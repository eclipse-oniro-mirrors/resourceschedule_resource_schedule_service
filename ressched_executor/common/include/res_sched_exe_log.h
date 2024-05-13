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

#ifndef RESSCHED_EXECUTOR_COMMON_INCLUDE_RES_SCHED_EXE_LOG_H
#define RESSCHED_EXECUTOR_COMMON_INCLUDE_RES_SCHED_EXE_LOG_H

#include "hilog/log.h"

namespace OHOS {
namespace ResourceSchedule {
#undef LOG_TAG
#define LOG_TAG "ResSchedExe"

#undef LOG_DOMAIN
#define LOG_DOMAIN 0xD001740

#define RSSEXE_LOGF(fmt, ...) HILOG_FATAL(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##__VA_ARGS__)
#define RSSEXE_LOGE(fmt, ...) HILOG_ERROR(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##__VA_ARGS__)
#define RSSEXE_LOGW(fmt, ...) HILOG_WARN(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##__VA_ARGS__)
#define RSSEXE_LOGI(fmt, ...) HILOG_INFO(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##__VA_ARGS__)
#define RSSEXE_LOGD(fmt, ...) HILOG_DEBUG(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##__VA_ARGS__)
} // namespace ResourceSchedule
} // namespace OHOS

#endif // RESSCHED_EXECUTOR_COMMON_INCLUDE_RES_SCHED_EXE_LOG_H
