# 架构原则 (Architecture)

## 设计原则

1. **决策与执行分离**：调度决策（SA 1901，无特权）与特权写操作（SA 1918，root）分进程隔离，实现权限收敛与安全边界。
2. **插件化扩展**：核心框架只做事件感知与分发，业务策略（cgroup、调频、待机、帧感知）以动态链接插件形式接入，产品可裁剪。
3. **事件驱动**：以 resType 为核心键的统一事件模型贯穿"感知→决策→执行"全链路，异步事件多播分发、同步事件单插件独占决策。
4. **配置可裁剪**：插件开关、插件私有配置、cgroup 映射均通过 XML/JSON 配置 + 多产品路径叠加，编译期 feature 开关进一步控制能力。
5. **时延敏感路径直通**：对低时延要求的频率写操作，执行器侧插件可绕过 SocPerf IPC 直接写 sysfs 节点。
6. **稳定性优先**：插件超时自动禁用与恢复、载荷限流、权限收敛、CFI/PAC/栈保护等安全构建项强制开启。

## 技术选型

| 维度 | 选型 | 理由 |
|------|------|------|
| 语言 | C/C++ 为主 | 系统级服务，需直接操作句柄、sysfs、cgroup；性能与内存可控 |
| 对外接口层 | C（`background_process_manager`）、NAPI（JS）、Taihe FFI（ETS） | 覆盖不同调用方生态 |
| 异步运行时 | FFRT + EventHandler 双路径 | FFRT 提供协程级低开销队列与延时任务；feature 关闭时回退到 EventHandler 保证可用性 |
| IPC | IDL 驱动生成 stub/proxy | 避免手写错误，保证接口码稳定与可评审 |
| 配置解析 | XML（libxml2）+ JSON（cJSON/nlohmann） | 插件配置用 XML 适合层级结构；cgroup 映射用 JSON 简洁；事件载荷用 nlohmann::json 灵活 |
| 插件加载 | dlopen + C ABI 导出符号 | 解耦编译期依赖，支持产品裁剪与运行时加载 |
| 日志/事件 | HiLog + HiSysEvent + HiTrace | 与系统 DFx 体系一致，支持故障定位与行为统计 |
| 安全 | CFI + PAC + stack-protector-strong + SELinux + AccessToken | 系统级安全合规要求 |

## 逻辑架构

### 两进程分层架构

```
┌──────────────────────── SA 1901: 决策进程 (uid=ressched) ────────────────────────┐
│                                                                                    │
│  事件感知层 (sched_controller)                                                      │
│  ├─ common_event: EventController (CommonEvent 订阅)                                │
│  └─ observer: 各类观察者 (app/window/audio/telephony/power/account/...)              │
│       ↓ ReportData / 同进程直连                                                     │
│  服务层 (resschedservice)                                                          │
│  ├─ ResSchedServiceAbility (SystemAbility 入口, SA 1901)                             │
│  └─ ResSchedService (Stub 实现: 权限校验/限流/分发)                                   │
│       ↓                                                                             │
│  框架层 (resschedmgr)                                                              │
│  ├─ ResSchedMgr (顶层管理, 扩展白名单, executor 初始化)                              │
│  ├─ PluginMgr (插件加载/卸载/订阅/分发/超时统计)                                     │
│  │   ├─ ConfigReader (res_sched_config.xml)                                         │
│  │   ├─ PluginSwitch (res_sched_plugin_switch.xml)                                  │
│  │   └─ PluginLib (dlopen handle + 函数指针)                                         │
│  ├─ EventListenerMgr (事件监听者管理, FFRT 队列)                                     │
│  ├─ NotifierMgr (系统负载等级通知, FFRT 队列)                                       │
│  ├─ KillProcess (客户端发起的进程杀)                                                │
│  └─ suspend_manager_base (进程挂起观察者管理)                                       │
│       ↓ 分发 ResData                                                                │
│  插件层 (plugins, .z.so)                                                           │
│  ├─ cgroup_sched_plugin: 进程分组决策 + Supervisor 世界模型                         │
│  ├─ socperf_plugin: 调频事件桥 → SocPerfClient                                      │
│  ├─ frame_aware_plugin: 帧感知调度 + 网络时延控制                                   │
│  └─ device_standby_plugin: 设备待机状态处理                                         │
│                                                                                    │
└────────────────────────────────────────────────────────────────────────────────────┘
                                       ↓ IPC (SendRequestSync/Async)
┌──────────────────────── SA 1918: 执行进程 (uid=root) ─────────────────────────────┐
│                                                                                    │
│  服务层 (resschedexeservice)                                                       │
│  ├─ ResSchedExeService (SystemAbility 入口, SA 1918)                                │
│  └─ ResSchedExeMgr (请求处理, cgroup/kill/插件)                                     │
│       ↓                                                                             │
│  执行器插件 (socperf_executor_plugin)                                               │
│  ├─ SocPerfExecutorWirteNode: 直写 sysfs 频率节点 (fdsan 标签)                      │
│  └─ 配置加载: ResourceNode / GovResNode / ResNode                                   │
│                                                                                    │
└────────────────────────────────────────────────────────────────────────────────────┘
                                       ↓ 写内核
┌──────────────────────── Linux Kernel ─────────────────────────────────────────────┐
│  /dev/cpuset/{top-app,foreground,background,system-background}/cgroup.procs         │
│  /dev/cpuctl/{...}/cgroup.procs                                                    │
│  /dev/frz/{Frozen,Thawed,DeepFrozen,DeepFrozenTemporary}/cgroup.procs              │
│  sysfs 频率节点 (CPU/GPU/DDR/NPU)                                                   │
└────────────────────────────────────────────────────────────────────────────────────┘
```

### 对外接口分层

| 层 | 接口 | 调用方 |
|----|------|--------|
| C++ innerkits | `ResSchedClient`（ReportData/ReportSyncEvent/KillProcess/订阅负载等级/事件监听）、`ResSchedExeClient`（SendRequestSync/Async/KillProcess）、`SuspendManagerBaseClient`（挂起状态查询/观察） | 系统 SA、框架内部 |
| C API | `OH_BackgroundProcessManager_*`（进程优先级/省电模式/清理后台） | 应用进程（API 17+） |
| NAPI | `systemload`（on/off/getLevel）、`backgroundProcessManager` | JS 应用 |
| ETS/Taihe FFI | `systemload_taihe`、`backgroundprocessmanager_taihe` | ArkTS 应用 |

### 关键类职责（仅列架构关键类）

| 类 | 所属层 | 职责 |
|----|--------|------|
| `ResSchedServiceAbility` | 服务层 | SA 1901 入口，`SystemAbility` 派生，发布 `ResSchedService` |
| `ResSchedService` | 服务层 | IDL Stub 实现，权限校验、限流、载荷大小校验、转发给 `ResSchedMgr` |
| `ResSchedMgr` | 框架层 | 顶层管理，扩展 resType 白名单、executor 插件初始化、应用状态查询 |
| `PluginMgr` | 框架层 | 插件 dlopen 加载/卸载、订阅表维护（resType/resValue/sync）、异步分发（FFRT 队列或 EventHandler）、同步投递、超时统计与自动禁用/恢复、按平均耗时排序分发 |
| `ConfigReader` / `PluginSwitch` | 框架层 | XML 配置解析与合并 |
| `EventListenerMgr` | 框架层 | 客户端注册的事件监听者管理，按 pid/eventType/group 分发，FFRT 队列发送 |
| `NotifierMgr` | 框架层 | 系统负载等级通知者管理 |
| `SchedController` | cgroup 插件 | cgroup 插件入口（`Plugin` 派生），持有 `Supervisor` 与 `CgroupEventHandler` |
| `CgroupAdjuster` | cgroup 插件 | 计算 `SchedPolicy`，支持多种调整来源（`AdjustSource` 枚举） |
| `Supervisor` | cgroup 插件 | 进程世界模型，uid→Application→ProcessRecord 层级，多 SceneBoard 支持 |
| `CgroupAction` | process_group | 加载 cgroup JSON 配置，映射 policy↔name，调用执行器写 cgroup |
| `SocPerfPlugin` | socperf 插件 | resType→cmdId 映射，调用 `SocPerfClient` 提频/限频 |
| `ResSchedExeService` | 执行器服务层 | SA 1918 入口，`ResSchedExeServiceStub` 派生 |
| `ResSchedExeMgr` | 执行器框架层 | 请求处理，cgroup 写、kill、插件初始化 |
| `SocPerfExecutorWirteNode` | 执行器插件 | 直写 sysfs 频率节点（fdsan 标签 `0xd001702`） |

## 架构设施

### FFRT 双路径
- **基础路径（始终编译）**：`ffrt::mutex`、`ffrt::queue`、`ffrt::task_handle`（延时任务）在多个模块无条件使用（如 `EventListenerMgr`、`NotifierMgr`、`CgroupEventHandler`、`SceneRecognizerMgr`、`NetworkLatencyController`）。
- **高级路径（feature `resource_schedule_service_with_ffrt_enable`）**：`PluginMgr` 启用每插件 FFRT 派发队列（`qos_user_interactive`）、超时统计、自动禁用/恢复逻辑；feature 关闭时回退到单一 `EventHandler`（`EventRunner::Create("rssDispatcher")`）。
- 编译期通过 `#ifdef RESOURCE_SCHEDULE_SERVICE_WITH_FFRT_ENABLE` 切换，mutex 成员在两条路径下重复声明（`ffrt::mutex` vs `std::mutex`）。

### 配置叠加机制
- `PluginMgr::GetAllRealConfigPath` 通过 `config_policy_utils` 的 `GetCfgDirList` 返回多产品配置路径列表。
- `ConfigReader::MergeConfigList` / `MergePluginConfigMap` 按插件名合并多份配置，后加载的覆盖先加载的。
- 插件运行时通过 `PluginMgr::GetInstance().GetConfig(pluginName, configName)` 读取，用完调用 `RemoveConfig` 释放。

### 执行器大报文分批
- `ResSchedExeClient` 内部 `ProcessJson` 检测报文大小，超过分批阈值时拆分多批 IPC 发送（`SendRequestBatches`），单次 IPC 消息亦有上限（具体阈值见 [coding-standards.md](coding-standards.md) 配置规范节）。

### 外部配置注入
- `PluginMgr::LoadGetExtConfigFunc` 通过 dlopen `libsuspend_manager_service.z.so` 并 dlsym `GetExtMultiConfig`，允许挂起管理服务向框架注入配置。

### SA 生命周期
- 两个 SA 均 `run-on-create: true`（init 阶段即启动）、`distributed: false`（本地）。
- `ResSchedServiceAbility` 在 `OnStart` 中 `Publish(service_)` 发布服务，并监听其它 SA 的添加/移除（`OnAddSystemAbility`/`OnRemoveSystemAbility`）以动态初始化/释放观察者。
- SA 启动耗时写 `SA_START_TIME` HiSysEvent。

## 非功能设计关键方案

### 性能
- **事件分发低时延**：目标从 `ResSchedClient::ReportData` 到 `PluginMgr::DispatchResource` <10ms（RK3568 基准）。
- **插件排序分发**：`PluginMgr` 按插件平均耗时排序后依次分发，慢插件不阻塞快插件过久。
- **直写节点**：socperf 执行器插件绕过 SocPerf IPC 直写 sysfs，消除一次 IPC 往返。
- **同进程直连**：观察者与框架同进程时走 `ReportDataInProcess` 绕过 IPC。
- **AccessToken LRU 缓存**：权限校验结果缓存，避免重复查表。
- **FFRT 队列 QoS**：派发队列使用 `qos_user_interactive`，保证前台交互场景优先调度。

### 可靠性
- **插件超时自愈**：超时触发 RepairPlugin，累计超时永久禁用并告警；阻塞队列的插件延迟恢复（具体阈值见 [plugin.md](entities/plugin.md)）。
- **载荷与速率保护**：载荷上限、请求速率限流、重复错误日志抑制（具体阈值见 [coding-standards.md](coding-standards.md) 权限与限流节）。
- **DeathRecipient**：客户端持有服务死亡监听，服务端持有观察者/监听者死亡监听，自动清理。
- **配置路径校验**：`realpath` + `PATH_MAX` 防越界，`IsValidPath` 防 dlopen 路径注入。

### 安全
- **进程隔离**：决策（ressched uid）与执行（root uid）分离；SELinux 上下文独立。
- **权限收敛**：五张白名单 + AccessToken + LRU 缓存；执行器权限极少（详见 init cfg）。
- **构建安全项**：CFI + cross-dso、PAC 返回地址保护、stack-protector-strong、version_script 收敛导出符号。
- **fdsan**：执行器写节点的 fd 带 fdsan 标签，检测 fd 误用。

### 可观测
- **HiLog**：分组件 tag/domain，隐私标记强制，函数名自动前缀（执行器侧）。
- **HiSysEvent**：domain `RSS`，固定事件目录，关键故障/行为/统计事件。
- **HiTrace**：分发链路 `StartTrace(HITRACE_TAG_APP, ...)`，`HitraceScoped` RAII。
- **Dump**：服务与插件支持 `dump`，SA profile `dump_level: 1`。

## 架构约束

> 完整约束清单见 [AGENTS.md](../../AGENTS.md) 项目宪法节。以下为架构层面关键约束：

1. **决策与执行分离**：SA 1901 不得直接写 cgroup/freezer/sysfs，必须经 SA 1918。
2. **IPC 接口 IDL 驱动**：禁止手写 stub/proxy；接口码变更需 `@leonchan5` 评审。
3. **同步事件单插件独占**：一个同步 resType 只能被一个插件订阅，避免多插件竞争 reply。
4. **配置与代码一致**：cgroup JSON 配置须与 init cfg 创建的目录一致；插件开关 XML 与实际加载的插件一致。
