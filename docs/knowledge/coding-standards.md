# 编码规范 (Coding Standards)

本页仅收录 **本项目特有** 的规则与规范。业界通用规范（如C++ Style、命名一致性、错误处理基本范式等）不再赘述。

## 命名约定

### 文件与目录
- 头文件与源文件统一使用 **snake_case**（如 `res_sched_service.h`、`plugin_mgr.cpp`）。
- 目录名小写、下划线分词（如 `resschedmgr`、`pluginbase`、`cgroup_sched_plugin`）。

### 标识符
| 类别 | 风格 | 示例 |
|------|------|------|
| 类 / 结构体 / 枚举类型 | PascalCase | `Plugin`、`PluginMgr`、`ResData`、`SchedPolicy` |
| 成员函数 | PascalCase | `Init()`、`DispatchResource()`、`LoadOnePlugin()` |
| 成员变量 | **trailing underscore**（尾下划线） | `pluginSwitch_`、`resTypeLibMap_`、`supervisor_` |
| 局部变量 | camelCase 或 snake_case | `pluginList`、`costTime`、`initStartTime` |
| 常量 / 宏 | UPPER_SNAKE_CASE，置于匿名命名空间或头文件 `constexpr` | `PAYLOAD_MAX_SIZE`、`DISPATCH_TIME_OUT` |
| 插件 C ABI 导出符号 | 固定名 `OnPluginInit` / `OnPluginDisable` / `OnDispatchResource` / `OnDeliverResource` / `OnDump` | — |
| 命名空间 | 两级 `OHOS::ResourceSchedule`，闭合处带注释 | — |

### Include Guard
- **禁止 `#pragma once`**。头文件必须使用 **路径式大写宏** 作为 include guard，宏名由文件在仓内的相对路径派生。
  - 例：`plugin.h` → `RESSCHED_SERVICES_RESSCHEDMGR_PLUGINBASE_INCLUDE_PLUGIN_H`
  - 例：`res_sched_log.h` → `RESSCHED_COMMON_INCLUDE_RES_SCHED_LOG_H`

## 单例与拷贝控制

- 单例优先使用 `DECLARE_SINGLE_INSTANCE` / `IMPLEMENT_SINGLE_INSTANCE` 宏（来自 `single_instance.h`）。
- SA 能力类使用 `DECLARE_SYSTEM_ABILITY` + `DECLARE_DELAYED_SINGLETON`。
- 持有资源或为单例的类必须加 `DISALLOW_COPY_AND_MOVE(ClassName)` 宏禁止拷贝/移动。

## 日志规范

### 分组件日志头
每个编译单元组拥有独立的日志头，定义独立的 `LOG_DOMAIN` 与 `LOG_TAG`，并提供分级宏：

| 日志头 | tag | domain | 宏前缀 | 是否自动带函数名 |
|--------|-----|--------|--------|------------------|
| `ressched/common/include/res_sched_log.h` | `RSS` | `0xD001700` | `RESSCHED_LOG*` | 否（调用方手传 `__func__`） |
| `ressched_executor/common/include/res_sched_exe_log.h` | `ResSchedExe` | `0xD001740` | `RSSEXE_LOG*` | 是（宏内置 `[%{public}s]` + `__FUNCTION__`） |
| `socperf_executor_plugin/.../socperf_log.h` | `socperf` | `0xD001703` | `SOC_PERF_LOG*` | 是 |
| `cgroup_sched_plugin/common/include/cgroup_sched_log.h` | `CGS` | `0xD001702` | `CGS_LOG*` | — |

宏定义采用 `#undef` 后重定义的写法，保证跨翻译单元幂等。

### 隐私标记强制
所有 HiLog 格式串中的字符串占位符 **必须显式标注** `%{public}s`（非敏感）或 `%{private}s`（敏感），**禁止裸 `%s`**。敏感数据（如部分 libPath）使用 `%{private}s`。

## HiSysEvent 规范

- 事件 domain 固定为 `RSS`，事件目录在 `ressched/hisysevent.yaml` 中声明，新增事件须同步更新该文件。
- 调用 `HiSysEventWrite` 时必须携带 `MODULE_NAME`、`SCENE_NAME`、`ERR_INFO` 等标准字段。
- 关键事件：`INIT_FAULT`（初始化失败，CRITICAL）、`TIMEOUT_ERR`（超时，CRITICAL）、`PLUGIN_DISABLE`（插件禁用，MINOR）、`ABNORMAL_ERR`（载荷超大/权限错误，CRITICAL）、`SERVICE_REQUEST_LIMIT`（限流，BEHAVIOR）、`HOT_EVENT`（TopN resType 统计）、`SA_START_TIME`（SA 启动耗时）。

## IPC 规范

### IDL 驱动
- **禁止手写 `IRemoteStub` / `IRemoteProxy`**。所有 IPC 接口必须以 `.idl` 文件描述，由 `idl_tool` 自动生成 stub/proxy。
- IDL 文件位于接口目录（如 `ressched/interfaces/innerkits/ressched_client/IResSchedService.idl`），构建时通过 `idl_gen_interface` 模板生成。
- 异步接口用 `[oneway]` 或 `MessageOption::TF_ASYNC`；同步默认。
- `sequenceable` 声明用于跨 Parcel 类型。

### 接口码评审
- `res_sched_ipc_interface_code.h` 与 `socperf_ipc_interface_code.h` 的任何修改必须经 `@leonchan5` 评审（`CODEOWNERS` 强制）。
- 接口码使用 `enum class`，编号稳定，不复用。

### 同进程直连
为避免 IPC 开销，框架提供 `extern "C"` 的进程内直连接口（`ReportDataInProcess` / `KillProcessInProcess` / `ReportSyncEventInProcess`），供同进程观察者直接调用 `ResSchedMgr` / `PluginMgr`。

## 插件规范

插件 C ABI 契约、dlopen 加载机制、效率与超时阈值、订阅 API 的完整说明见 [plugin.md](entities/plugin.md)。以下为编码层面的强制规则：

- 插件 `.so` 必须导出 `extern "C"` 的 `OnPluginInit` / `OnPluginDisable`（其余符号可选），签名详见 [plugin.md](entities/plugin.md)。
- 使用 `dlopen(path, RTLD_NOW)` 加载，handle 用 `std::shared_ptr<void>(handle, dlclose)` 包裹实现 RAII 自动卸载。
- 加载前必须经 `ResCommonUtil::IsValidPath` 校验路径合法性。
- 插件路径来自 `res_sched_plugin_switch.xml` 的 `libpath` 属性。
- 插件原则上只接收事件并转发到独立服务进程/线程处理，禁止在 `OnDispatchResource` 中做耗时业务。
- 默认每个插件只给框架增加一个线程（IPC 转发）；多线程须评审。

## 配置规范

### 配置文件位置
| 文件 | 作用 | 解析方 |
|------|------|-------|
| `etc/ressched/res_sched_plugin_switch.xml` | 插件开关与 libpath | `PluginSwitch`（libxml2） |
| `etc/ressched/res_sched_config.xml` | 插件私有配置 | `ConfigReader`（libxml2） |
| `etc/cgroup_sched/cgroup_action_config.json` | cgroup 策略↔分组映射 | `CgroupAction`（cJSON） |

### 多产品配置叠加
- 通过 `config_policy_utils.h` 的 `GetCfgDirList()` / `CheckRealPath()` 解析多产品配置路径，返回路径列表。
- `ConfigReader` 的 `MergeConfigList` / `MergePluginConfigMap` 按插件名合并多份配置。
- 路径校验基于 `realpath`，长度受 `PATH_MAX` 约束。

### XML 解析约定
- 使用 `xmlReadMemory(..., XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING)`。
- 插件开关根标签 `pluginlist`，子节点 `plugin` 含 `libpath` / `libpathexe`（执行器插件）/ `switch` 属性。
- 插件配置根标签 `ressched`，层级 `plugin name` → `config name` → `item`（带 properties）→ `subitem`。

### 载荷格式
- 事件载荷在 IPC 边界为 `unordered_map<string, string>`，框架内转为 `nlohmann::json`。
- 推荐载荷字段：`pid`、`uid`、`bundleName`。
- 单次 IPC 载荷 ≤ 4096 字节（`PAYLOAD_MAX_SIZE`）；执行器大报文按 180000 字节分批（`IPC_BATCHES_SIZE`），单次 IPC 消息上限 200000 字节（`MAX_IPC_MESSAGE_SIZE`）。

## 安全构建项（强制）

每个共享库与测试目标必须配置：
```gn
sanitize = { cfi = true; cfi_cross_dso = true; debug = false }
branch_protector_ret = "pac_ret"
cflags_cc = [ "-fstack-protector-strong" ]
```
并使用 `version_script`（如 `libresschedsvc.versionscript`）收敛导出符号。

## 测试规范

- 单元测试使用 **GoogleTest + GoogleMock**，目标类型 `ohos_unittest`。
- 测试编译统一加 `-Dprivate=public -Dprotected=public` 暴露私有成员。
- Fuzz 测试入口 `LLVMFuzzerTestOneInput`，使用 `FuzzedDataProvider`，目标类型对应 `ohos_fuzztest`。
- 测试类须设置 native token 并申请所需权限（如 `REPORT_RESOURCE_SCHEDULE_EVENT`、`DUMP`）。
- 测试用例注释格式：`/** @tc.name: ..., @tc.desc: ..., @tc.require: issueIXXXXX */`。
- 新增插件/事件必须同步补齐 `test/unittest` 与（按需）`test/fuzztest`。

## 权限与限流

### 上报方权限校验
服务端按五张白名单校验上报方权限：
- `allowSCBReportRes_` — SceneBoard 可上报的 resType
- `allowAllSAReportRes_` — 所有 SA 可上报的 resType
- `allowSomeSAReportRes_` — 特定 SA 可上报的 resType
- `allowAllAppReportRes_` — 所有应用可上报的 resType
- `allowFgAppReportRes_` — 前台应用可上报的 resType

AccessTokenID 经 LRU 缓存（`permissionCache_`）加速查表。所需权限：`ohos.permission.REPORT_RESOURCE_SCHEDULE_EVENT`。

### 限流
- 单 uid：250 次/秒（`SINGLE_UID_REQUEST_LIMIT_COUNT` / `LIMIT_REQUEST_TIME=1000ms`）。
- 全量：650 次/秒（`ALL_UID_REQUEST_LIMIT_COUNT`）。
- 超限写 `SERVICE_REQUEST_LIMIT` HiSysEvent；重复错误日志按 5s 间隔抑制（`errLogRecordMap_`）。

## 进程与 SA 标识

- 决策 SA：`1901`，进程 `resource_schedule_service`，uid `ressched`，SELinux `u:r:resource_schedule_service:s0`。
- 执行 SA：`1918`，进程 `resource_schedule_executor`，uid `root`，SELinux `u:r:resource_schedule_executor:s0`。
- SA profile JSON 中 `run-on-create: true`、`distributed: false`。
- SA 启动耗时须写 `SA_START_TIME` HiSysEvent。

## 事件编号稳定

- `res_type.h` 中已分配的 resType 编号 **不可复用、不可挪动**。
- 新增异步事件追加在 `ASYNC_RES_TYPE_LAST` 之前；新增同步事件追加在 `SYNC_RES_TYPE_LAST` 之前。
- 改事件须同步：补订阅、补测试、补文档。

## 禁止项

- 禁止 `.cpp` 中遗留 `TODO` / `FIXME` / `HACK`（当前代码库无此类标记）。
- 禁止日志/配置/文档中出现密钥、口令、内部账号。
- 禁止在 SA 1901（决策进程）内直接写 cgroup/freezer/sysfs 特权节点。
