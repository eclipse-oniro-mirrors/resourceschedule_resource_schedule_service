/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_KILL_REASON_H
#define RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_KILL_REASON_H

#include <string>

namespace OHOS {
namespace ResourceSchedule {
class KillReason {
public:
    static constexpr char CPU_HIGHLOAD[] = "Kill Reason:CPU Highload";
    static constexpr char CPU_EXT_HIGHLOAD[] = "Kill Reason:CPU_EXT Highload";
    static constexpr char IO_MANAGE_CONTROL[] = "Kill Reason:IO Manage Control";
    static constexpr char ION_MANAGE_CONTROL[] = "Kill Reason:ResourceLeak:Ion Leak";
    static constexpr char MEMORY_SOFT_DETERIORATION[] = "Kill Reason:ResourceLeak:Pss Soft Kill";
    static constexpr char MEMORY_DETERIORATION[] = "Kill Reason:ResourceLeak:Pss Kill";
    static constexpr char GPU_RS_HIGHLOAD[] = "Kill Reason:ResourceLeak:Gpu_rs Leak";
    static constexpr char GPU_HIGHLOAD[] = "Kill Reason:ResourceLeak:Gpu Leak";
    static constexpr char VMA_LEAK_HIGHLOAD[] = "Kill Reason:ResourceLeak:Vma Leak";
    static constexpr char FD_HIGHLOAD[] = "Kill Reason:ResourceLeak:Fd Leak";
    static constexpr char THREAD_HIGHLOAD[] = "Kill Reason:ResourceLeak:Thread Leak";
    static constexpr char ASHMEM_HIGHLOAD[] = "Kill Reason:ResourceLeak:Ashmem Leak";
    static constexpr char MEMORY_PRESSURE[] = "Kill Reason:Memory Pressure";
    static constexpr char TEMPERATURE_CONTROL[] = "Kill Reason:Temperature Control";
    static constexpr char RESOURCE_OVERLIMIT[] = "Kill Reason:Resource Overlimit";
    static constexpr char STANDBY_CLEAN[] = "Kill Reason:ResourceLeak:Standby Clean";
    static constexpr char KERNEL_ZONE[] = "Kill Reason:Kernel Zone Low";
    static constexpr char HEAVYLOAD_MUTEX_KILL[] = "Kill Reason:HeavyLoad:Mutex Kill";
};
} // namespace ResourceSchedule
} // namespace OHOS

#endif // RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_KILL_REASON_H
