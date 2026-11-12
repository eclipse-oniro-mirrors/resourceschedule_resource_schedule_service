# 领域术语 (Glossary)

本页收录资源调度服务项目中 **AI 容易理解偏差或仅在本项目语境下才有特定含义** 的术语。业界通用概念（如 IPC、SA、HiLog、cgroup 基础概念等）不再赘述，请参见 OpenHarmony 官方文档。

## 调度核心术语

| 术语 | 含义 | 易混淆点 / 注意 |
|------|------|----------------|
| **resType（资源事件类型）** | 一类系统事件的唯一编号，定义于 `res_type.h`，是整个事件分发体系的核心键。分异步（0–222）与同步（500–524）两段。 | 不是"资源类型"，而是"资源调度事件类型"。一个 resType 通常对应一个业务场景（如应用启动、亮屏、滑动识别）。 |
| **ResData（事件数据）** | 事件在框架内的统一载体：`{resType, value, payload, reply}`。`value` 是事件子值（如开/关、状态枚举值），`payload` 是 nlohmann::json 载荷。 | `reply` 仅同步事件使用，用于返回决策结果给调用方。异步事件不使用 reply。 |
| **resValue（事件子值）** | 同一 resType 下的状态/场景区分值。例如 `RES_TYPE_SCREEN_STATUS` 的 value 0=灭屏、1=亮屏。 | 部分插件可按 (resType, resValue) 精确订阅（`SubscribeResourceAccurately`），而非仅按 resType 订阅。 |
| **SchedPolicy（调度策略/分组策略）** | 进程的 cgroup 调度分组标识，取值：`SP_DEFAULT=0`、`SP_BACKGROUND=1`、`SP_FOREGROUND=2`、`SP_SYSTEM_BACKGROUND=3`、`SP_TOP_APP=4`。 | "system" 在 README 中指系统常驻进程的分组，对应代码中的 `SP_SYSTEM_BACKGROUND`，不是 `SP_DEFAULT`。`SP_DEFAULT` 实际等同于 root/未分组。 |
| **Supervisor（进程世界模型）** | cgroup 插件维护的"全局进程状态视图"，持有 uid→Application→ProcessRecord 的层级结构，记录每个进程的窗口、能力、音频、相机、任务、焦点等状态。 | 不是 Linux supervisor 守护进程。它是 cgroup 调度决策的数据基座。 |
| **ProcessRecord（进程记录）** | Supervisor 中对单个进程的状态记录，包含其当前/上次调度分组、瞬态任务、音频播放/采集、视频、截屏、画图、nap、连续任务、running lock、AV codec、关键线程角色等数十个状态字段。 | 一个 Application（uid）下可有多个 ProcessRecord（多进程）。 |
| **AdjustSource（调整来源）** | cgroup 调整的触发来源枚举（`AdjustSource` enum，含前台应用变化、能力/扩展状态、进程创建、瞬态/连续任务、窗口聚焦/可见性、关键线程、音频、nap、截屏、系统负载、应用预加载、亮灭屏、效率资源、UIExtension 预加载等）。用于追踪分组决策链路。 | 仅用于 dump/追溯，不影响分组结果本身。 |

## 系统调频术语

| 术语 | 含义 | 易混淆点 / 注意 |
|------|------|----------------|
| **SocPerf（统一调频服务）** | OpenHarmony 的 SoC 性能调频服务（独立组件 `soc_perf`），负责 CPU/GPU/DDR/NPU 频率仲裁与设置。本项目通过插件调用其客户端接口。 | SocPerf 是**独立组件**，不在本仓内；本仓内的 `socperf_plugin` 是它的"事件桥"。 |
| **cmdId（调频命令 ID）** | SocPerf 的核心参数，连接调频场景与调频配置。在 `socperf_boost_config.xml` 中定义，必须全局唯一。 | 与 resType 不同：一个 resType 可映射到多个 cmdId（如点击场景提频 + 滑动场景提频）。 |
| **Boost / Limit（提频 / 限频）** | Boost = 提升频率以提升性能；Limit = 限制频率以控制功耗/温度。两者互斥：Limit 期间 Boost 无法突破上限。 | `PowerLimitBoost` / `ThermalLimitBoost` 是"设置限频开关"，不是直接提频。 |
| **ON/OFF 调频** | 长期生效、需手动开关的调频模式，对应 `PerfRequestEx(cmdId, onOffTag, ...)`；与一次性提频 `PerfRequest(cmdId, ...)` 区分。 | onOffTag=true 开启、false 关闭；必须配对调用，否则频率不会回落。 |
| **resID（资源 ID）** | `socperf_resource_config.xml` 中定义的可调资源的唯一标识，每个资源有单/多写节点路径。 | 与 cmdId、resType 三者均不同维度。 |

## 架构与进程术语

| 术语 | 含义 | 易混淆点 / 注意 |
|------|------|----------------|
| **决策进程（SA 1901）** | `resource_schedule_service` 进程（uid `ressched`），负责事件感知、分组决策、插件加载与事件分发。无 root 权限。 | 不能在此进程直接写 cgroup/freezer/sysfs 特权节点。 |
| **执行进程（SA 1918）** | `resource_schedule_executor` 进程（uid `root`），负责执行特权写操作（cgroup、freezer、频率节点）。 | 决策与执行分离是本项目的核心架构约束，目的是权限收敛与安全隔离。 |
| **FFRT** | Functional Flow Runtime，OpenHarmony 的协程/任务异步运行时。本项目用其 `queue`、`mutex`、`submit`、`task_handle`（延时任务）。 | `resource_schedule_service_with_ffrt_enable` 默认 false：基础 FFRT（mutex/queue）仍编译生效；该开关仅控制**每插件 FFRT 派发队列、超时统计、自动禁用/恢复**等高级能力。 |
| **PluginStat（插件统计）** | PluginMgr 对每个插件的运行统计：分发次数、累计耗时、平均耗时、超时次数。用于按平均耗时排序分发、定位慢插件。 | 非性能埋点，是框架内部分发调度依据。 |
| **SceneBoard（场景板）** | OpenHarmony 的窗口/桌面渲染进程，多桌面场景下可存在多个 SceneBoard 实例（以 uid+pid 对标识）。 | 单桌面时代 Supervisor 只记一个 sceneBoardUid/Pid；多桌面改造后改为 `set<pair<uid,pid>>`。 |

## 后台进程与挂起术语

| 术语 | 含义 | 易混淆点 / 注意 |
|------|------|----------------|
| **BackgroundProcessManager（后台进程管理）** | 对外暴露的 C/NAPI/ETS 接口，提供设置/重置进程后台优先级、省电模式、清理后台应用能力。SysCap `BackgroundProcessManager`。 | 与 cgroup 调度是上下游关系：接口设置优先级 → 内部走 cgroup 调整。 |
| **ProcessPriority（进程优先级，C API）** | C API 中的优先级枚举：`PROCESS_BACKGROUND=1`、`PROCESS_INACTIVE=2`。 | 与 SchedPolicy 不是一回事，是面向应用层的简化语义。 |
| **PowerSaveMode（省电模式，C API）** | C API 枚举：`EFFICIENCY_MODE=1`、`DEFAULT_MODE=2`。 | 是进程级省电模式，不是系统级省电模式。 |
| **Suspend（挂起 / Freeze）** | 后台进程冻结（cgroup freezer）。状态机：`ACTIVE_STATE=0` → `DOZE_STATE` → `FROZEN_STATE`。支持观察者订阅状态变更。 | 与"进程被杀"不同：freeze 是可恢复的挂起，进程仍在但不调度执行。 |
| **SystemLoadLevel（系统负载等级）** | 融合的系统负载等级事件，可被客户端订阅（`RegisterSystemloadNotifier`）。用于让上层感知系统繁忙程度并自适应行为。 | 不是简单的 CPU 利用率，是综合调度状态的等级。 |

## 事件订阅术语

| 术语 | 含义 | 易混淆点 / 注意 |
|------|------|----------------|
| **SubscribeResource（订阅）** | 插件在 `Init()` 中声明关注某 resType，框架在事件到达时分发。 | 异步事件分发，多个插件可订阅同一 resType。 |
| **SubscribeResourceAccurately（精确订阅）** | 按 (resType, resValue) 精确订阅，仅当事件 value 匹配时才分发。 | 用于同一 resType 有多个子状态、插件只关心其中部分的情况。 |
| **SubscribeSyncResource（同步订阅）** | 同步事件订阅，**一个同步 resType 只能被一个插件独占订阅**。 | 同步事件需要 reply 返回值，故不能多插件竞争。 |
| **ReportData（上报）** | 客户端/观察者向 SA 1901 异步上报事件（`ResSchedClient::ReportData`）。 | 同进程可走 `ReportDataInProcess` 直连框架，绕过 IPC。 |
| **ReportSyncEvent（同步上报）** | 同步上报事件并等待 reply（`ResSchedClient::ReportSyncEvent`）。 | 用于需要框架决策结果的场景（如查询挂起状态、互斥检查）。 |
| **DeliverResource（同步投递）** | 同步事件到达插件时的回调（`OnDeliverResource`），区别于异步的 `OnDispatchResource`。 | 插件可选实现；未实现则返回 0。 |
