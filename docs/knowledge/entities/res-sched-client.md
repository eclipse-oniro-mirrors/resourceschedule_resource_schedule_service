# 业务实体：InnerAPI（对外客户端接口）

## 概念

本项目对外提供多套 InnerAPI（innerkits），供系统 SA、框架、应用进程与资源调度服务交互。按调用目标分为决策侧（SA 1901）、执行器侧（SA 1918）、进程分组、后台进程管理、系统负载订阅五组。

## ResSchedClient（决策侧客户端）

`ResSchedClient`（`ressched_client` innerkits）封装与 SA 1901 的 IPC 交互：

| 接口 | 说明 |
|------|------|
| `ReportData(resType, value, mapPayload)` | 异步上报事件，无返回值 |
| `ReportSyncEvent(resType, value, payload, reply)` | 同步上报事件，等待 reply 返回（支持 json 与 map 两种载荷重载） |
| `KillProcess(mapPayload)` | 请求杀进程 |
| `RegisterSystemloadNotifier(callback)` | 注册系统负载等级监听 |
| `UnRegisterSystemloadNotifier(callback)` | 注销系统负载等级监听 |
| `GetSystemloadLevel()` | 获取当前系统负载等级 |
| `RegisterEventListener(listener, eventType, group)` | 注册事件监听者 |
| `UnRegisterEventListener(listener, eventType, group)` | 注销事件监听者 |
| `IsAllowedAppPreload(bundleName, preloadMode)` | 查询是否允许应用预加载 |
| `IsAllowedLinkJump(isAllowed)` | 查询是否允许链接跳转 |
| `IsAllowedKill(callerUid, reason)` | 查询 kill reason 是否允许 |

### 同进程直连

观察者与框架同进程时，可绕过 IPC 直接调用 `extern "C"` 接口：
- `ReportDataInProcess(resType, value, payload)` — 直连 `ResSchedMgr`
- `KillProcessInProcess(payload)` — 直连
- `ReportSyncEventInProcess(resType, value, payload, reply)` — 直连

## ResSchedExeClient（执行器客户端）

`ResSchedExeClient`（`resschedexe_client` innerkits）封装与 SA 1918 的 IPC 交互：

| 接口 | 说明 |
|------|------|
| `SendRequestSync(resType, value, context, reply)` | 同步请求执行器（resType 为 `ResExeType` 枚举值） |
| `SendRequestAsync(resType, value, context)` | 异步请求执行器 |
| `KillProcess(pid)` | 请求执行器杀指定 pid 进程 |

> 执行器内部对大报文按分批阈值拆分发送（`SendRequestBatches`）。执行器事件类型 `ResExeType` 定义于 `res_exe_type.h`，与决策侧 `ResType` 独立。

## SuspendManagerBaseClient（挂起状态查询）

`SuspendManagerBaseClient`（`suspend_manager_base_client` innerkits）封装与 SA 1901 的同步 IPC 交互，查询/观察进程挂起（freeze）状态：

| 接口 | 说明 |
|------|------|
| `GetSuspendStateByUid(uid, &isFrozen)` | 按 uid 查询应用挂起状态（false=活跃, true=冻结） |
| `GetSuspendStateByPid(pid, &isFrozen)` | 按 pid 查询进程挂起状态 |
| `RegisterSuspendObserver(observer)` | 注册挂起状态观察者，状态变更时回调 |
| `UnregisterSuspendObserver(observer)` | 注销挂起状态观察者 |

> 挂起状态由 cgroup freezer 驱动，状态机：`ACTIVE_STATE`(0) → `DOZE_STATE` → `FROZEN_STATE`。观察者接口 `ISuspendStateObserverBase` 须由调用方实现。

## process_group API（进程分组设置）

`libprocess_group.z.so` 提供进程/线程 cgroup 分组设置的 C 接口（定义于 `sched_policy.h`）：

| 函数 | 说明 |
|------|------|
| `SetThreadGroupSchedPolicy(pid, policy)` | 设置进程（线程组）的 cgroup |
| `SetThreadSchedPolicy(tid, policy)` | 设置单线程的 cgroup |
| `GetThreadSchedPolicy(tid, &policy)` | 查询线程当前策略 |
| `GetSchedPolicyByName(name, &policy)` | 按名称查策略 |
| `GetSchedPolicyShortenedName(policy)` | 策略短名 |
| `GetSchedPolicyFullName(policy)` | 策略全名 |
| `AddSchedPolicyDeclaration(...)` | 声明自定义策略 |

> 需要 root 权限的写操作经 SA 1918 执行：`CgroupAction::SetSchedPolicyByExecutor(tid, policy, resType)` → IPC 到执行器 → 写 `/dev/cpuset/<policy>/cgroup.procs`。

## C API：BackgroundProcessManager

`background_process_manager`（`libbackground_process_manager.z.so`，Kit `BackgroundTasksKit`）对外暴露 C API（SysCap `BackgroundProcessManager`），供应用进程设置后台优先级与省电模式：

| 接口 | 说明 | @since |
|------|------|--------|
| `OH_BackgroundProcessManager_SetProcessPriority(pid, priority)` | 设置进程后台优先级（`PROCESS_BACKGROUND`=1 / `PROCESS_INACTIVE`=2） | 17 |
| `OH_BackgroundProcessManager_ResetProcessPriority(pid)` | 重置进程后台优先级 | 17 |
| `OH_BackgroundProcessManager_SetPowerSaveMode(pid, powerSaveMode)` | 设置进程省电模式（`EFFICIENCY_MODE`=1 / `DEFAULT_MODE`=2） | 20 |
| `OH_BackgroundProcessManager_IsPowerSaveMode(pid)` | 查询进程是否处于省电模式 | 20 |
| `OH_BackgroundProcessManager_GetPowerSaveMode(pid, &powerSaveMode)` | 获取进程省电模式 | 22 |
| `OH_BackgroundProcessManager_ClearBackgroundApps(clearType)` | 清理后台应用（`CLEAR_RECENT_CARDS`=1） | — |

## NAPI / ETS 接口

| 模块 | SysCap | 接口 |
|------|--------|------|
| `systemload` | `SystemLoad` | `on(type, callback)` / `off(type, callback)` / `getLevel()` |
| `backgroundProcessManager` | `BackgroundProcessManager` | 后台优先级/省电模式的 JS 封装 |

> ETS 侧通过 Taihe FFI 生成对应接口（`systemload_taihe`、`backgroundprocessmanager_taihe`）。

## 约束

- 所有 IPC 接口均基于 IDL 自动生成 stub/proxy，禁止手写。
- 接口码（`res_sched_ipc_interface_code.h`、`socperf_ipc_interface_code.h`）变更须 `@leonchan5` 评审。
- 单次 IPC 载荷与执行器大报文均有上限（具体阈值见 [coding-standards.md](../coding-standards.md) 配置规范节）。
- 上报方须持 `ohos.permission.REPORT_RESOURCE_SCHEDULE_EVENT` 权限并按五张白名单校验（详见 [coding-standards.md](../coding-standards.md) 权限与限流节）。
