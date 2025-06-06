/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "window_state_observer.h"
#include "nlohmann/json.hpp"
#include "res_sched_log.h"
#include "res_sched_mgr.h"
#include "res_type.h"

namespace OHOS {
namespace ResourceSchedule {
void PiPStateObserver::OnPiPStateChanged(const std::string& bundleName, const bool isForeground)
{
    RESSCHED_LOGI("Receive OnPiPStateChange %{public}s %{public}d", bundleName.c_str(), isForeground);
    nlohmann::json payload;
    ResSchedMgr::GetInstance().ReportData(ResType::RES_TRPE_PIP_STATUS,
        static_cast<int64_t>(isForeground), payload);
}
} // namespace ResourceSchedule
} // namespace OHOS