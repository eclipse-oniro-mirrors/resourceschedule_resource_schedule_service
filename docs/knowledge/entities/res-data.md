# 业务实体：事件模型（ResData / ResType）

## 概念

资源调度框架以 **resType（事件类型编号）** 为核心键，将系统事件封装为 **ResData（事件数据）** 实例，按插件订阅关系分发。这是整个"感知→决策→执行"链路的统一事件模型。

## ResData 结构

| 字段 | 类型 | 说明 |
|------|------|------|
| `resType` | `uint32_t` | 事件类型编号，对应 `ResType` 枚举值 |
| `value` | `int64_t` | 事件子值/状态值。同一 resType 下区分不同状态（如亮屏=1/灭屏=0），或承载 cmdId 等数值参数 |
| `payload` | `nlohmann::json` | 事件载荷，结构化数据。IPC 边界为 `unordered_map<string,string>`，框架内转为 json |
| `reply` | `nlohmann::json*` | 同步事件的返回值指针，由被投递的插件填充。异步事件不使用（为空） |

此外定义了 `ResPair = pair<uint32_t, int64_t>`（resType + value 组合），并为其提供 `std::hash` 特化，用于精确订阅表。

## ResType 编号规则

定义于 `res_type.h`，分异步与同步两段，编号区间分离：

| 段 | 实际事件编号 | 哨兵常量 | 调用方式 |
|----|------------|---------|---------|
| 异步事件 | 0 – 222 | `RES_TYPE_FIRST`=0；`ASYNC_RES_TYPE_LAST`=223（末尾哨兵） | `ReportData`，无返回值，多插件可订阅 |
| 同步事件 | 500 – 524 | `SYNC_RES_TYPE_FIRST`=`ASYNC_RES_TYPE_LAST`=223（首个实际事件为 500）；`SYNC_RES_TYPE_LAST`=525（末尾哨兵） | `ReportSyncEvent`，带 reply，单插件独占订阅 |

> `SYNC_RES_TYPE_FIRST` 在枚举中等于 `ASYNC_RES_TYPE_LAST`（223），但实际同步事件编号从 500 开始（224–499 为异步事件预留增长空间）。

**稳定性约束**：已分配编号不可复用、不可挪动；新增异步事件追加在 `ASYNC_RES_TYPE_LAST` 之前，新增同步事件追加在 `SYNC_RES_TYPE_LAST` 之前。完整事件清单见 `res_type.h`。

### 异步事件（按业务域归类，仅示例）

| 业务域 | 代表事件 |
|--------|---------|
| 屏幕与电源 | `RES_TYPE_SCREEN_STATUS`(0)、`RES_TYPE_POWER_MODE_CHANGED`(92)、`RES_TYPE_BATTERY_LOW`(215) |
| 应用与能力生命周期 | `RES_TYPE_APP_STATE_CHANGE`(1)、`RES_TYPE_ABILITY_STATE_CHANGE`(2)、`RES_TYPE_APP_ABILITY_START`(27) |
| 窗口与 UI | `RES_TYPE_WINDOW_FOCUS`(5)、`RES_TYPE_WINDOW_VISIBILITY_CHANGE`(12)、`RES_TYPE_SPLIT_SCREEN`(79)、`RES_TRPE_PIP_STATUS`(135) |
| 手势与交互 | `RES_TYPE_CLICK_RECOGNIZE`(9)、`RES_TYPE_SLIDE_RECOGNIZE`(11)、`RES_TYPE_LOAD_PAGE`(34)、`RES_TYPE_CROWN_ROTATION_STATUS`(129) |
| 线程与 QoS | `RES_TYPE_REPORT_KEY_THREAD`(39)、`RES_TYPE_THREAD_QOS_CHANGE`(23)、`RES_TYPE_REPORT_SCENE_BOARD`(38) |
| 音频与媒体 | `RES_TYPE_AUDIO_RENDER_STATE_CHANGE`(24)、`RES_TYPE_CALL_STATE_UPDATE`(22)、`RES_TYPE_AV_SESSION_ON_SESSION_CREATE`(52) |
| 相机 | `RES_TYPE_REPORT_CAMERA_STATE`(35)、`RES_TYPE_CAMERA_STATUS_CHANGE`(147) |
| 性能与调频 | `RES_TYPE_SOCPERF_CUST_EVENT_BEGIN`(67)、`RES_TYPE_KEY_PERF_SCENE`(72)、`RES_TYPE_FIRST_FRAME_DRAWN`(211) |
| 待机与挂起 | `RES_TYPE_DEVICE_IDLE`(111)、`RES_TYPE_NAP_MODE`(178) |

### 同步事件（500–524）

| 代表事件 | 编号 | 说明 |
|---------|------|------|
| `SYNC_RES_TYPE_REQUEST_MUTEX_STATUS` | 500 | 请求互斥状态 |
| `SYNC_RES_TYPE_GET_SUSPEND_STATE_BY_UID` | 506 | 按 uid 查挂起状态 |
| `SYNC_RES_TYPE_GET_SUSPEND_STATE_BY_PID` | 507 | 按 pid 查挂起状态 |
| `SYNC_RES_TYPE_THAW_ONE_APP` | 517 | 解冻单个应用 |
| `SYNC_RES_TYPE_GET_ALL_SUSPEND_STATE` | 518 | 获取全部挂起状态 |
| `SYNC_RES_TYPE_GET_APP_TYPE` | 523 | 获取应用类型 |

> 完整同步事件清单见 `res_type.h`。

## 执行器事件类型（ResExeType）

执行器进程（SA 1918）使用独立的事件类型枚举 `ResExeType`（定义于 `res_exe_type.h`），与上述 `ResType` 不同：

| 段 | 代表类型 | 编号 | 说明 |
|----|---------|------|------|
| 同步 | `RES_TYPE_COMMON_SYNC` | 0 | 通用同步 |
| | `EWS_TYPE_SOCPERF_EXECUTOR_SYNC_EVENT` | 2 | socperf 直写同步 |
| | `RES_TYPE_CGROUP_SYNC_EVENT` | 3 | cgroup 读节点同步 |
| | `RES_TYPE_SET_THREAD_GROUP_SCHED_POLICY_SYNC_EVENT` | 6 | 设置线程组调度策略同步 |
| | `RES_TYPE_SYNC_END` | 9 | 同步段哨兵 |
| 异步 | `RES_TYPE_COMMON_ASYNC` | 9 | 通用异步（= `RES_TYPE_ASYNC_FIRST`） |
| | `EWS_TYPE_SOCPERF_EXECUTOR_ASYNC_EVENT` | 11 | socperf 直写异步 |
| | `RES_TYPE_STANDBY_EXECUTOR_ASYNC_EVENT` | 14 | 设备待机执行器异步 |
| | `RES_TYPE_LAST` / `RES_TYPE_DEBUG` | 1000 | 末尾哨兵 / debug |

> 执行器事件通过 `ResSchedExeClient` 上报，与决策侧的 `ResSchedClient` 体系独立。完整枚举见 `res_exe_type.h`。客户端接口详见 [res-sched-client.md](res-sched-client.md)。

## 事件流转

### 异步事件（无 reply）
事件源调用 `ResSchedClient::ReportData` → 框架构造 `ResData{resType, value, json(payload), nullptr}` → 按订阅表投递给各插件的 `OnDispatchResource`。分发时框架为每个插件克隆一份独立的 `shared_ptr<ResData>`。

### 同步事件（带 reply）
事件源调用 `ResSchedClient::ReportSyncEvent` → 框架构造 `ResData{resType, value, json(payload), &reply}` → 投递给**唯一**订阅该同步 resType 的插件的 `OnDeliverResource`，插件填充 `*reply` 后返回。

## 载荷约定

- 推荐 payload 字段：`pid`、`uid`、`bundleName`。
- 载荷与报文大小受限，超限返回错误码（具体阈值见 [coding-standards.md](../coding-standards.md) 配置规范节）。

## 约束

- `resType` 必须是 `res_type.h` 中已定义的合法编号。
- 编号稳定，不复用、不挪动；新增须同步补插件订阅、补测试、补文档。
- 异步事件可被多个插件订阅；同步事件只能被一个插件独占订阅。
- payload 在 IPC 边界只能是 `string→string` 映射，复杂数据须序列化为字符串。
- 扩展 resType 白名单由 `resource_schedule_service_with_ext_res_enable` feature 控制。
