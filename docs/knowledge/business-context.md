# 业务上下文 (Business Context)

## 业务背景

OpenHarmony 是面向全场景的分布式操作系统，需要在手机、平板、穿戴、大屏、车机等多种设备形态上运行。这些设备的 SoC 资源（CPU 核数、频率、GPU、DDR 带宽）有限，而系统中同时存在数十个进程（前台应用、后台应用、系统服务、渲染进程、音频/相机/电话业务等）。如何在**保证前台体验流畅**与**控制后台功耗**之间取得平衡，是系统资源调度的核心命题。

`resource_schedule_service` 正是承载这一命题的调度引擎。它的业务目标可归纳为：

1. **场景化提频**：感知到用户感知强烈的场景（应用启动、点击、滑动、页面加载、动画、游戏等）时，及时提升 CPU/GPU/DDR 频率以保流畅；场景结束后及时回落以省电。
2. **进程分级调度**：依据应用前后台、窗口焦点、后台任务状态，将进程划分到不同 cgroup 调度分组，使前台进程获得更多 CPU 资源、后台进程被限制甚至冻结。
3. **限频保护**：在高温、低电量等受限工况下，限制最高频率以保护硬件与续航。
4. **后台进程治理**：对外提供后台进程优先级设置、省电模式、进程挂起状态查询与观察的能力，支撑系统级后台治理策略。
5. **系统负载感知**：对外提供系统负载等级订阅，让上层应用据系统繁忙程度自适应行为。

本项目是 OpenHarmony 资源调度子系统（`resourceschedule`）的核心组件，但不孤立存在——它的事件来源于众多其它子系统，其调频能力依赖独立的 SocPerf 组件，其进程分组能力依赖 Linux cgroup 内核能力。

## 系统上下文

### 外部依赖系统

本项目依赖（并交互）以下 OpenHarmony 子系统/组件，按交互方向分类：

#### 事件来源（向 RSS 上报事件的系统）

| 外部系统 | 提供的事件 | 接入方式 |
|----------|-----------|---------|
| **AppMgr / AbilityMgr**（ability_runtime、bundle_framework） | 应用/能力/扩展/进程状态变化、应用启动、安装卸载、冷启动 | 观察者（`app_state_observer`）+ ReportData |
| **WindowManager**（window_manager） | 窗口焦点、可见性、绘制内容、尺寸、移动、分屏、悬浮窗、画中画 | 观察者（`window_state_observer`），默认用完整 window_manager（feature `resource_schedule_service_depend_wm_enable` 为 false 时回退 WindowManagerLite） |
| **AudioFramework**（audio_framework） | 音频渲染状态、铃声模式、音量键、音频场景、采集状态、输出设备 | 观察者（`audio_observer`），多类音频回调 |
| **AVSession**（av_session） | 媒体会话创建/释放/顶会话切换 | 观察者（`av_session_state_listener`），feature 控制 |
| **Telephony**（core_service、state_registry） | 通话状态更新 | 观察者（`sched_telephony_observer`） |
| **PowerManager**（power_manager） | 亮灭屏、锁屏、电源模式、RunningLock 状态 | 观察者（`display_power_event_observer`）+ ReportData |
| **Account**（os_account） | 账号激活/切换/移除 | 观察者（`account_observer`） |
| **BackgroundTaskMgr**（background_task_mgr） | 瞬态任务、连续任务 | ReportData |
| **Input（MMI）**（input） | 触摸、按键、表冠旋转、轴事件、手势动画 | ReportData（应用侧打点上报）+ MMI 观察者（app_nap） |
| **Connectivity**（netmanager_base、bluetooth、wifi） | 网络、蓝牙 A2DP、WiFi 连接/P2P/电源、近距 | 观察者 + CommonEvent 订阅（`event_controller`） |
| **Camera**（camera_framework） | 相机状态、镜头状态、优先级重置 | ReportData（`RES_TYPE_REPORT_CAMERA_STATE` 等） |
| **Thermal**（thermal_manager，外部） | 热状态、热场景 | ReportData |
| **HiSysEvent**（hisysevent） | 应用无响应/异常等事件（app_nap 场景） | HiSysEvent 观察者（feature 控制） |
| **CommonEventService**（common_event_service） | 电池、锁屏、克隆态、媒体控制、包增删、连接、音频焦点、近距广播 | CommonEvent 订阅（`event_controller`） |
| **DisplayManager**（display_manager，外部） | 折叠屏模式、屏幕方向、多屏 | 观察者（`fold_display_mode_observer`） |
| **Request**（request） | 下载/上传任务 | 观察者（`download_upload_observer`），feature 控制 |
| **DistributedSchedule**（distributed_schedule，外部） | 分布式续播 | 观察者（`continuation_observer`），feature 控制 |

#### 能力消费（RSS 调用的系统）

| 外部系统 | 用途 | 调用方 |
|----------|------|-------|
| **SocPerf**（soc_perf） | CPU/GPU/DDR/NPU 频率仲裁与设置 | `socperf_plugin` 调用 `SocPerfClient` |
| **Linux cgroup**（内核） | cpuset/cpuctl/freezer 进程分组 | 经 SA 1918 执行器写 `/dev/cpuset`、`/dev/cpuctl`、`/dev/frz` |
| **Linux sysfs**（内核） | SoC 频率节点直写（低时延路径） | SA 1918 的 `socperf_executor_plugin` 直写节点 |
| **AccessToken**（access_token） | 上报方权限校验 | `ResSchedService` |
| **BundleMgr**（bundle_framework） | uid ↔ bundleName 查询 | `ResBundleMgrHelper` |
| **Samgr / Safwk**（samgr、safwk） | SA 注册、发现、生命周期 | `ResSchedServiceAbility` / `ResSchedExeService` |
| **ConfigPolicy**（config_policy） | 多产品配置路径解析 | `PluginMgr::GetAllRealConfigPath` |
| **DataShare**（data_share） | OOBE 克隆配置读取 | `OobeDatashareUtils` |
| **Init**（init） | 系统参数、cgroup 目录创建、SA 启动 | init cfg |

#### 被消费（外部调用 RSS 的系统）

| 外部调用方 | 调用接口 | 用途 |
|-----------|---------|------|
| ACE / ArkUI（图形 UI 框架） | `ReportData`（封装 `ressched_report.h`） | 上报点击、滑动、页面加载等 UI 事件用于提频 |
| 各系统 SA（相机、音频、电话等） | `ReportData` | 上报业务状态事件 |
| SceneBoard / 桌面 | `ReportData` | 上报窗口、焦点、分屏事件 |
| 应用进程 | NAPI/ETS/C API | 设置自身后台优先级、省电模式、订阅负载等级 |
| 系统治理类 SA（memmgr、device_standby 等） | `RegisterSuspendObserver` / `GetSuspendStateByUid/Pid` | 感知/控制进程挂起状态 |
| 系统上层 | `RegisterSystemloadNotifier` / `GetSystemloadLevel` | 订阅系统负载等级 |

### 交互上下文

#### 进程拓扑

```
┌──────────────────────────────────────────────────────────────┐
│  系统事件源（AppMgr/WM/Audio/Telephony/Power/MMI/Camera...）  │
│           ↓ ReportData / ReportSyncEvent / 观察者回调          │
├──────────────────────────────────────────────────────────────┤
│  SA 1901: resource_schedule_service (uid=ressched)            │
│  ┌─────────────┐  ┌────────────┐  ┌────────────────────────┐ │
│  │ 观察者集合   │→│ ResSchedMgr│→│ PluginMgr (dlopen 插件) │ │
│  │ (各类观察者) │  │            │  │  - cgroup_sched        │ │
│  └─────────────┘  └────────────┘  │  - socperf_plugin      │ │
│                                   │  - frame_aware_plugin  │ │
│                                   │  - device_standby      │ │
│                                   └──────────┬─────────────┘ │
│                                              │ 决策结果        │
├──────────────────────────────────────────────┼───────────────┤
│  SA 1918: resource_schedule_executor (uid=root)              │
│  ┌─────────────────────────────────────────┐  │              │
│  │ ResSchedExeMgr → 执行器插件              │←─┘              │
│  │  - socperf_executor_plugin (直写节点)    │                 │
│  │  - cgroup/freezer 写操作                 │                 │
│  └─────────────────────────────────────────┘                 │
│         ↓ 直接写内核                                          │
├──────────────────────────────────────────────────────────────┤
│  Linux Kernel: /dev/cpuset, /dev/cpuctl, /dev/frz, sysfs      │
└──────────────────────────────────────────────────────────────┘
        ↑
   SocPerf SA (独立组件) ← socperf_plugin 调用
```

#### 关键交互流

**1. 异步事件提频流（典型：点击提频）**

```
ACE 点击识别打点
  → ResSchedClient::ReportData(RES_TYPE_CLICK_RECOGNIZE, ...)
  → [IPC] SA 1901 ResSchedService::ReportData
  → 权限校验（白名单 + AccessToken LRU）
  → ResSchedMgr::ReportData → PluginMgr::DispatchResource
  → 按 resType 查订阅表 → 投递到 socperf_plugin 的 FFRT/EventHandler 队列
  → socperf_plugin HandleEventClick → SocPerfClient::PerfRequest(cmdId, msg)
  → [IPC] SocPerf SA → 仲裁 → 写频率节点
```

**2. 同步事件决策流（典型：查询挂起状态）**

```
外部 SA
  → ResSchedClient::ReportSyncEvent(SYNC_RES_TYPE_GET_SUSPEND_STATE_BY_UID, ...)
  → [IPC] SA 1901 ResSchedService::ReportSyncEvent
  → ResSchedMgr → PluginMgr::DeliverResource (同步，单插件独占)
  → 插件 OnDeliverResource 填充 reply
  → [IPC 返回] reply 回到调用方
```

**3. cgroup 调整流（典型：应用进入前台）**

```
AppMgr 上报应用状态变化
  → cgroup_event_handler 接收
  → 更新 Supervisor 中的 Application/ProcessRecord 状态
  → CgroupAdjuster::AdjustProcessGroup 计算 SchedPolicy
  → CgroupAction::SetSchedPolicyByExecutor(tid, policy, resType)
  → [IPC] SA 1918 ResSchedExeClient::SendRequestSync(RES_TYPE_CGROUP_SYNC_EVENT, ...)
  → 执行器写 /dev/cpuset/<policy>/cgroup.procs
```

**4. 后台进程优先级设置流（C API）**

```
应用进程
  → OH_BackgroundProcessManager_SetProcessPriority(pid, PROCESS_BACKGROUND)
  → [内部映射] → SA 1901
  → cgroup 调整到 SP_BACKGROUND
```

### 约束性边界

- **决策与执行权限隔离**：SA 1901（决策进程，uid `ressched`）无 root 权限，所有特权写经 SA 1918（执行进程，uid `root`）。
- **载荷与速率受限**：单次 IPC 载荷与请求速率均有上限（具体阈值见 [coding-standards.md](coding-standards.md) 权限与限流节）。
- **权限收敛**：上报方须持 `REPORT_RESOURCE_SCHEDULE_EVENT` 权限并按五张白名单细分（详见 [coding-standards.md](coding-standards.md)）。
- **插件时效**：插件事件处理须快速返回，超时触发告警/禁用/永久禁用（具体阈值见 [plugin.md](entities/plugin.md) 效率与内存规范节）。
