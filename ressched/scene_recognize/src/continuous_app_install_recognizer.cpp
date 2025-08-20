/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#include "continuous_app_install_recognizer.h"
#include "ffrt_inner.h"
#include "res_sched_log.h"
#include "res_sched_mgr.h"
#include "res_type.h"

namespace OHOS {
namespace ResourceSchedule {
namespace {
    constexpr int64_t EXIT_INSTALL_DELAY_TIME = 50 * 1000 * 1000;
}

ContinuousAppInstallRecognizer::ContinuousAppInstallRecognizer()
{
    AddAcceptResTypes({
        ResType::RES_TYPE_APP_INSTALL_UNINSTALL,
    });
}

ContinuousAppInstallRecognizer::~ContinuousAppInstallRecognizer()
{
    RESSCHED_LOGI("~ContinuousAppInstallRecognizer");
}

void ContinuousAppInstallRecognizer::OnDispatchResource(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (resType != ResType::RES_TYPE_APP_INSTALL_UNINSTALL) {
        return;
    }
    if (value == ResType::AppInstallStatus::APP_INSTALL_START) {
        if (exitAppInstall_) {
            ffrt::skip(exitAppInstall_);
        }
        if (!isInContinuousInstall_.load()) {
            nlohmann::json payload;
            ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_CONTINUOUS_INSTALL,
                ResType::ContinuousInstallStatus::START_CONTINUOUS_INSTALL, payload);
            isInContinuousInstall_.store(true);
        }
    } else if (value == ResType::AppInstallStatus::APP_INSTALL_END ||
        value == ResType::AppInstallStatus::APP_CHANGED) {
        if (exitAppInstall_) {
            ffrt::skip(exitAppInstall_);
        }
        exitAppInstall_ = ffrt::submit_h([recognizer = shared_from_this()]() {
            nlohmann::json payload;
            ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_CONTINUOUS_INSTALL,
                ResType::ContinuousInstallStatus::STOP_CONTINUOUS_INSTALL, payload);
            recognizer->isInContinuousInstall_.store(false);
        }, {}, {}, ffrt::task_attr().delay(EXIT_INSTALL_DELAY_TIME));
    }
}
} // namespace ResourceSchedule
} // namespace OHOS