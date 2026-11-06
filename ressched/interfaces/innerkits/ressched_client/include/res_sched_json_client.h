/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd.
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

#ifndef RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_JSON_CLIENT_H
#define RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_JSON_CLIENT_H

#include "res_sched_client.h"
#include "nlohmann/json.hpp"

namespace OHOS {
namespace ResourceSchedule {

class ResSchedJsonClient {
public:
    static ResSchedJsonClient& GetInstance();
    int32_t ReportSyncEvent(const uint32_t resType, const int64_t value, const nlohmann::json& payload,
        nlohmann::json& reply);
private:
    ResSchedJsonClient() = default;
    ~ResSchedJsonClient() = default;
    DISALLOW_COPY_AND_MOVE(ResSchedJsonClient);
};
} // namespace ResourceSchedule
} // namespace OHOS

#endif // RESSCHED_INTERFACES_INNERKITS_RESSCHED_CLIENT_INCLUDE_RES_SCHED_JSON_CLIENT_H
