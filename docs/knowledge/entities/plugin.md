# 业务实体：Plugin（插件）

## 概念

插件是资源调度框架的**业务策略承载单元**。框架本身只负责事件感知与分发，具体的调度策略（cgroup 分组、调频、帧感知、设备待机）由独立编译的动态链接库（`.z.so`）以插件形式接入。插件通过 `dlopen` 加载，导出固定的 C ABI 符号，框架在加载后按订阅关系向其分发事件。

## 插件契约（C ABI）

每个插件 `.so` 必须导出以下 `extern "C"` 符号：

| 符号 | 必选 | 签名 | 说明 |
|------|------|------|------|
| `OnPluginInit` | 是 | `bool(std::string& libName)` | 初始化，返回 true 才视为加载成功；libName 为插件路径 |
| `OnPluginDisable` | 是 | `void()` | 插件退出/被禁用时回调，用于清理资源 |
| `OnDispatchResource` | 否（典型有） | `void(const std::shared_ptr<ResData>&)` | 异步事件分发回调 |
| `OnDeliverResource` | 否 | `int32_t(const std::shared_ptr<ResData>&)` | 同步事件投递回调，返回值即决策结果；未实现返回 0 |
| `OnDump` | 否 | `void(const std::vector<std::string>&, std::string&)` | dump 支持 |

框架内部通过函数指针 typedef 持有这些符号（`OnPluginInitFunc`、`OnDispatchResourceFunc`、`OnDeliverResourceFunc`、`OnDumpFunc`、`OnPluginDisableFunc`）。此外 `plugin_mgr.h` 还定义了 `OnIsAllowedAppPreloadFunc`（应用预加载查询）、`GetExtMultiConfigFunc`（外部配置注入）、`OnInitFinishCallback`（初始化完成回调）等非插件契约的内部 typedef。

## 抽象基类

插件实现侧通常继承 `Plugin` 抽象基类（`pluginbase/include/plugin.h`），其声明以下方法：
- `Init()` — 纯虚，加载时调用
- `Disable()` — 纯虚，卸载时调用
- `DispatchResource(const std::shared_ptr<ResData>&)` — 纯虚，异步事件
- `DeliverResource(const std::shared_ptr<ResData>&)` — 虚（默认返回 0），同步事件

`OnPluginInit` 等导出函数内部通常调用单例的 `Init()` 等。

## 生命周期

```
1. 服务启动 (SA 1901 OnStart)
   → PluginMgr::Init()
   → PluginSwitch::LoadXmlPluginSwitch()  解析 res_sched_plugin_switch.xml
   → 对每个 switch=on 的插件:
      a. ResCommonUtil::IsValidPath 校验路径
      b. dlopen(libPath, RTLD_NOW)
      c. dlsym OnPluginInit / OnPluginDisable (必选) + OnDispatchResource / OnDeliverResource / OnDump (可选)
      d. 调用 OnPluginInit(libPath) → 必须返回 true
      e. handle 用 shared_ptr<void>(handle, dlclose) 包裹 (RAII)
      f. 插件在 Init() 中调用 SubscribeResource 订阅事件
   → 所有插件加载完成 → 调用 OnOnInitFinishCallbacks

2. 事件到达
   → PluginMgr::DispatchResource(resType, value, payload)
   → 查 resTypeLibMap_ / resTyperesValueLibMap_ / resTypeLibSyncMap_
   → 按 PluginStat 平均耗时排序插件列表
   → 投递到各插件的 FFRT 队列 (feature 开启) 或 EventHandler
   → 调用 OnDispatchResource(make_shared<ResData>(...)) (每个插件独立拷贝)
   → 计时: >10ms 告警, >50ms RepairPlugin

3. 同步事件到达
   → PluginMgr::DeliverResource → 唯一订阅插件的 OnDeliverResource → 填充 reply

4. 插件超时自愈
   → RepairPlugin: 单次 >50ms 触发禁用+重新 Init
   → 60s 内累计 3 次超时 → 永久禁用 + PLUGIN_DISABLE HiSysEvent
   → 阻塞队列的插件延迟恢复 (pluginBlockTime = 10 分钟)

5. 服务停止 / 插件卸载
   → 调用 OnPluginDisable() → dlclose (shared_ptr 析构)
```

## 订阅 API

插件在 `Init()` 中通过 `PluginMgr` 单例订阅：

| API | 说明 |
|-----|------|
| `SubscribeResource(libName, resType)` | 按 resType 订阅异步事件；多插件可订阅同一 resType |
| `SubscribeResourceAccurately(libName, resType, resValue)` | 按 (resType, resValue) 精确订阅；仅 value 匹配时分发 |
| `SubscribeSyncResource(libName, resType)` | 订阅同步事件；**一个同步 resType 只能被一个插件独占订阅** |

## 配置访问

插件可通过 `PluginMgr::GetInstance().GetConfig(pluginName, configName)` 读取 `res_sched_config.xml` 中本插件的私有配置，返回 `PluginConfig`（含 `itemList`，每个 `Item` 含 `subItemList`）。用完调用 `RemoveConfig` 释放。

配置结构（XML）：
```
ressched
└─ plugin name="pluginName"
   └─ config name="configName"
      └─ item [attr=val,...]
         └─ subitem name="..." value="..."
```

## 现有插件

| 插件 | 库 | 职责 |
|------|----|------|
| **cgroup_sched_plugin** | `libcgroup_sched.z.so` | 进程 cgroup 分组决策；维护 Supervisor 世界模型；经执行器写 cgroup |
| **socperf_plugin** | `libsocperf_plugin.z.so` | resType→cmdId 映射；调用 `SocPerfClient` 提频/限频 |
| **frame_aware_plugin** | `libframe_aware_plugin.z.so` | 帧感知调度；网络时延控制 |
| **device_standby_plugin** | `libdevice_standby_plugin.z.so` | 设备待机状态处理（feature `RSS_DEVICE_STANDBY_ENABLE`） |
| **socperf_executor_plugin** | `libsocperf_executor_plugin.z.so` | 执行器侧直写 sysfs 频率节点（SA 1918 内，feature `resource_schedule_service_socperf_executor_enable`） |

## 效率与内存规范

- **单次事件处理目标 <1ms**（开发期消除 WARNING）；**>10ms 告警**（`DISPATCH_WARNING_TIME`）；**>50ms 触发 RepairPlugin**（`DISPATCH_TIME_OUT`）。
- **60s 内 3 次超时永久禁用**（`MAX_PLUGIN_TIMEOUT_TIMES` / `DISABLE_PLUGIN_TIME`）。
- 插件加载超时 1000ms（`PLUGIN_LOAD_TIMEOUT`）。
- 插件原则上只接收事件并转发到独立服务进程/线程处理，禁止在 `OnDispatchResource` 中做耗时业务。
- 默认每插件只给框架增加一个线程（IPC 转发）；多线程须评审。
- 新增插件须评审 `resource_schedule_service` 进程内存基线（评审人：黄昌华），测试数据记入 check_list。
- monkey 测试要求每 24h WARNING ≤3 次。

## 使用方式（插件开发者视角）

1. 继承 `Plugin` 基类，实现 `Init` / `Disable` / `DispatchResource`（+ 可选 `DeliverResource`）。
2. 导出 `extern "C"` 的 `OnPluginInit` / `OnPluginDisable` / `OnDispatchResource` 等符号。
3. 在 `Init()` 中调用 `SubscribeResource` 订阅关注的事件。
4. 在 `DispatchResource` 中快速处理（或转发到独立线程/服务），返回。
5. 在 `res_sched_plugin_switch.xml` 中配置 `<plugin libpath="..." switch="on"/>`。
6. （可选）在 `res_sched_config.xml` 中配置插件私有配置。
7. 补齐 `test/unittest` 与 `test/fuzztest`。
8. 参与内存基线评审。

详见 `Resource_Schedule_Service_Process_Management_Specifications_ZH.md`。
