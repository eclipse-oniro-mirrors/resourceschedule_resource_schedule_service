# AGENTS.md — 资源调度服务项目宪法

> 本文件是 `resource_schedule_service` 项目的最高约束文档。任何在本仓内进行开发、修改、生成代码的智能体或开发者，必须先阅读并遵循本文件。

## 一、项目定位

### 做什么

`resource_schedule_service` 是 OpenHarmony 资源调度子系统（subsystem `resourceschedule`）下的核心调度引擎。它负责：

1. **事件感知与分发**：感知系统事件（应用生命周期、窗口焦点、亮灭屏、音频、电话、相机、网络等异步与同步事件），并按插件订阅关系分发给资源调度插件。
2. **进程分组调度决策**：依据应用状态、窗口状态、后台任务状态，决策每个进程的 cgroup 调度策略（top-app / foreground / background / system-background），并落到 Linux cgroup（cpuset/cpuctl/freezer）。
3. **统一调频执行**：将提频/限频事件转换为 SocPerf 指令，或经执行器进程直接写内核节点，完成 CPU/GPU/DDR 频率调节。
4. **后台进程管理与挂起**：对外提供后台进程优先级设置、省电模式、进程挂起（freeze）状态查询与观察的能力。

对外暴露的系统能力（SysCap）有两个：
- `SystemCapability.Resourceschedule.BackgroundProcessManager`（后台进程管理）
- `SystemCapability.ResourceSchedule.SystemLoad`（系统负载等级订阅）

### 技术栈

| 维度 | 选型 |
|------|------|
| 语言 | C/C++（服务、插件、innerkits）；C（`background_process_manager` 对外 C API）；JS/ETS（NAPI/Taihe FFI 对外 JS API） |
| 构建系统 | GN + Ninja（OpenHarmony 标准构建）；产物按 `bundle.json` 组织 |
| IPC | IDL 驱动，由 `idl_tool` 自动生成 stub/proxy；基于 `ipc` 与 `samgr`、`safwk` |
| 异步运行时 | FFRT（Functional Flow Runtime），feature `resource_schedule_service_with_ffrt_enable` 控制高级派发；非 FFRT 路径回退到 `AppExecFwk::EventHandler` |
| 配置格式 | 插件开关/插件私有配置为 XML（libxml2 解析）；cgroup 映射为 JSON（cJSON 解析）；事件载荷为 nlohmann::json |
| 插件机制 | 动态链接库（`.z.so`），通过 `dlopen` 加载，C ABI 导出固定符号 |
| 日志/事件 | HiLog（domain `0xD001700` / tag `RSS`）；HiSysEvent（domain `RSS`）；HiTrace |
| 安全 | CFI + PAC 返回地址保护 + stack-protector-strong；SELinux 上下文隔离；AccessToken 权限校验 |

### 关键标识

- 组件名：`@ohos/resource_schedule_service`，版本 3.1
- 决策 SA：`1901`（进程 `resource_schedule_service`，uid `ressched`）
- 执行 SA：`1918`（进程 `resource_schedule_executor`，uid `root`）
- 子系统：`resourceschedule`，仅适配 `standard` 系统

## 二、项目宪法

### 必须遵循的约束

1. **代码与文档一致**：所有文档（含 `docs/knowledge/*`、README）描述的功能规格、约束、阈值必须与代码实现一致；发现不一致时以代码为准并修正文档，禁止杜撰。
2. **插件契约不可破坏**：插件必须导出 `extern "C"` 的 `OnPluginInit` / `OnPluginDisable`（`OnDispatchResource` 典型有、`OnDeliverResource` / `OnDump` 可选）。新增插件须遵循 `Resource_Schedule_Service_Process_Management_Specifications_ZH.md` 的内存与效率规范，并参与内存基线评审（评审人：黄昌华）。
3. **IPC 接口码变更需评审**：`res_sched_ipc_interface_code.h`、`socperf_ipc_interface_code.h` 的任何修改必须经 `@leonchan5` 评审（见 `CODEOWNERS`）。
4. **事件 resType 编号稳定**：`res_type.h` 中已分配的编号不可复用或挪动；新增异步事件追加在 `ASYNC_RES_TYPE_LAST` 之前，新增同步事件追加在 `SYNC_RES_TYPE_LAST` 之前。
5. **权限校验必做**：服务端 `ReportData` / `KillProcess` 等接口必须按五张白名单校验上报方权限，且通过 AccessTokenID LRU 缓存加速（白名单名称与限流阈值见 [coding-standards.md](docs/knowledge/coding-standards.md) 权限与限流节）。
6. **载荷大小受限**：单次 IPC 载荷与执行器大报文均有上限（具体阈值见 [coding-standards.md](docs/knowledge/coding-standards.md) 配置规范节）。
7. **日志隐私标记**：所有 HiLog 格式串必须显式标注 `%{public}s` 或 `%{private}s`，禁止裸 `%s`。
8. **构建安全项强制开启**：每个共享库/测试目标必须配置 `sanitize = { cfi = true; cfi_cross_dso = true; debug = false }`、`branch_protector_ret = "pac_ret"`、`cflags_cc = ["-fstack-protector-strong"]`，并使用 `version_script` 收敛导出符号。
9. **测试规范**：单元测试使用 GoogleTest/gmock，编译期以 `-Dprivate=public -Dprotected=public` 暴露私有成员；新增插件/事件须同步补齐 `test/unittest` 与（按需）`test/fuzztest`。
10. **新增插件须评估进程内存基线**：`resource_schedule_service` 进程存在内存基线，新增插件必须重新评审基线内存，并将测试数据记入 check_list。

### 禁止做的事

1. **禁止在插件中做耗时业务**：插件只负责接收事件并快速转发到独立服务进程/线程处理；禁止在 `OnDispatchResource` 中执行阻塞或耗时任务（超时阈值与禁用规则见 [plugin.md](docs/knowledge/entities/plugin.md) 效率与内存规范节）。
2. **禁止在 RSS 框架进程内随意新增线程**：默认插件只给框架增加一个线程（用于 IPC 转发）；如需多线程须说明原因并参与评审。
3. **禁止绕过执行器写特权节点**：需要 root 权限的 cgroup/freezer/sysfs 写操作必须经 SA 1918（执行器）执行，禁止在 SA 1901（决策）进程内直接写。
4. **禁止手写 IPC stub/proxy**：IPC 接口必须以 `.idl` 描述并由 `idl_tool` 生成，禁止手写 `IRemoteStub`/`IRemoteProxy`。
5. **禁止提交敏感信息**：日志、配置、文档中不得出现密钥、口令、内部账号等；`OAT.xml` 已配置开源审计过滤。
6. **禁止使用裸 `#pragma once`**：头文件必须使用路径式大写宏作为 include guard。
7. **禁止在 `.cpp` 中遗留 TODO/FIXME/HACK**：当前代码库无此类标记，新增代码须保持整洁。

## 三、构建验证

### 编译

采用 OpenHarmony 标准全量构建（在源码树根目录执行）：

```bash
# 编译 resource_schedule_service 组件（在 OpenHarmony 源码根目录）
./build.sh --product-name <产品名> --build-target resource_schedule_service
```

关键 GN 目标（来自 `bundle.json` 的 `build.group_type`）：

| 分组 | 目标 | 产物 |
|------|------|------|
| base_group | `ressched_plugin_config`、`ressched_plugin_switch`、`process_group_config` | 配置文件（XML/JSON） |
| fwk_group | `ressched_client`、`resschedexe_client`、`systemload`、`systemload_taihe`、`backgroundprocessmanager_napi`、`backgroundprocessmanager_taihe`、`background_process_manager`、`ressched_common_utils` | 客户端库与 NAPI/C/ETS 接口 |
| service_group | `resschedsvc`、`socperf_plugin`、`socperf_executor_plugin_all`、`frame_aware_plugin`、`device_standby_plugin`、`ressched_sa_profile`、`resource_schedule_service.cfg`、`resschedexesvc`、`resschedexe_sa_profile`、`resource_schedule_executor.cfg`、`cgroup_sched`、`libprocess_group` | 服务 `.so`、插件 `.so`、SA profile、init cfg |

主要产物动态库：
- `libresschedsvc.z.so` — SA 1901 服务实现
- `libresschedexesvc.z.so` — SA 1918 执行器服务实现
- `libressched_client.z.so` / `libresschedexe_client.z.so` — 客户端
- `libcgroup_sched.z.so` / `libprocess_group.z.so` — cgroup 调度
- `libsocperf_plugin.z.so` / `libframe_aware_plugin.z.so` / `libdevice_standby_plugin.z.so` / `libsocperf_executor_plugin.z.so` — 插件

### feature 开关（`ressched/ressched.gni` 中 `declare_args`）

| feature | 默认 | 作用 |
|---------|------|------|
| `resource_schedule_service_with_ffrt_enable` | false | 启用每插件 FFRT 队列派发、超时统计、自动禁用/恢复 |
| `resource_schedule_service_with_ext_res_enable` | false | 扩展 resType 白名单 |
| `resource_schedule_service_socperf_executor_enable` | true | 启用执行器侧 socperf 直写节点插件（声明于 `ressched_executor/ressched_executor.gni`） |
| `resource_schedule_service_with_app_nap_enable` | false | 启用 app nap（HiSysEvent/MMI 观察者） |
| `resource_schedule_service_depend_wm_enable` | true | 窗口观察者依赖完整 window_manager（false 时用 WindowManagerLite） |
| `resource_schedule_service_has_sys_nice_enable` | false | 进程获得 SYS_NICE capability |
| `resource_schedule_service_distributed_continuation_enable` | false | 分布式续播观察者 |
| 其余 crown_power_key / cust_soc_perf / file_copy_soc_perf / subscribe_click_recognize / forkall_plugin / system_load_level_debug_for_2d 等 | false | 各场景特性（完整名称见 `bundle.json` features 与 `ressched.gni` declare_args） |

### 测试

```bash
# 单元测试（GoogleTest）
./build.sh --product-name <产品名> --build-target unittest
# 在设备上执行（atest 或直接调用）
atest unittest
```

测试目标清单见 `bundle.json` 的 `build.test`，覆盖：
- `ressched/test/unittest`、`ressched_executor/test/unittest`、`ressched/common/test/unittest`
- 各插件 `test/unittest`（cgroup_sched_plugin、socperf_plugin、frame_aware_plugin、socperf_executor_plugin）
- `ressched/sched_controller/test/unittest`
- 各模块 `test/fuzztest`（libFuzzer，入口 `LLVMFuzzerTestOneInput`）
- `ressched/interfaces/kits/test/unittest/systemload_jsunittest`（JS 单测）

测试编译统一加 `-Dprivate=public -Dprotected=public` 以访问私有成员。

### 验证清单（修改后必须自检）

1. 改 IPC 接口 → 同步改 `.idl`，并通知 `@leonchan5` 评审
2. 改事件 resType → 同步改 `res_type.h`、补订阅、补测试、补文档
3. 改插件 → 跑对应 `test/unittest` 与 `test/fuzztest`
4. 改 cgroup 配置 → 校验 `cgroup_action_config.json` 与 init cfg 一致
5. 改构建 → 校验 `bundle.json`、各 `BUILD.gn`、`.gni` 一致
6. 任何修改 → 运行 `sanitize`（CFI）编译通过

## 四、知识路由

本项目的领域知识文档位于 `docs/knowledge/`，按以下主题路由：

| 想了解 | 去看 |
|--------|------|
| 项目整体架构、设计原则、技术选型、非功能方案 | [docs/knowledge/architecture.md](docs/knowledge/architecture.md) |
| 业务背景、外部依赖系统、交互上下文 | [docs/knowledge/business-context.md](docs/knowledge/business-context.md) |
| 领域术语（cgroup、SocPerf、resType、Supervisor 等） | [docs/knowledge/glossary.md](docs/knowledge/glossary.md) |
| 项目特有编码规范（命名、日志、IPC、插件、安全构建项） | [docs/knowledge/coding-standards.md](docs/knowledge/coding-standards.md) |
| 事件模型（ResData / ResType） | [docs/knowledge/entities/res-data.md](docs/knowledge/entities/res-data.md) |
| 对外 InnerAPI（ResSchedClient / ResSchedExeClient / SuspendManagerBaseClient / process_group / C API / NAPI） | [docs/knowledge/entities/res-sched-client.md](docs/knowledge/entities/res-sched-client.md) |
| 插件契约与生命周期 | [docs/knowledge/entities/plugin.md](docs/knowledge/entities/plugin.md) |

其它参考文档（仓内根目录）：
- `README.md` / `README_ZH.md` — 面向用户的插件开发指南与架构简介
- `Resource_Schedule_Service_Process_Management_Specifications_ZH.md` — 插件内存与执行效率规范（新增插件必读）
- `bundle.json` — 组件依赖、构建目标、feature、测试清单
- `CODEOWNERS` — 强制评审人规则
- `OAT.xml` — 开源审计过滤配置
