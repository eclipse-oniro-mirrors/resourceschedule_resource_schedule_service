/*
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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
#include "slide_recognizer_proxy.h"
#include "slide_recognizer.h"
#include "display_manager.h"
#include "res_common_util.h"
#include "res_sched_log.h"
#include "res_type.h"

#include <charconv>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace OHOS {
namespace ResourceSchedule {

static const std::unordered_set<uint32_t> SLIDE_PROXY_ACCEPT_TYPES = {
    ResType::RES_TYPE_SLIDE_RECOGNIZE,
    ResType::RES_TYPE_SEND_FRAME_EVENT,
    ResType::RES_TYPE_CLICK_RECOGNIZE,
    ResType::RES_TYPE_AXIS_EVENT,
};

static const char* PAYLOAD_KEY_SCREEN_ID = "screenId";
static const char* PAYLOAD_KEY_DISPLAY_ID = "displayId";


SlideRecognizerProxy::SlideRecognizerProxy()
{
    AddAcceptResTypes(SLIDE_PROXY_ACCEPT_TYPES);
}

SlideRecognizerProxy::~SlideRecognizerProxy()
{
    std::lock_guard<std::mutex> lock(instancesMutex_);
    instances_.clear();
}

std::optional<uint64_t> SlideRecognizerProxy::ExtractScreenIdFromPayload(const nlohmann::json& payload)
{
    if (payload.contains(PAYLOAD_KEY_SCREEN_ID) && payload[PAYLOAD_KEY_SCREEN_ID].is_string()) {
        auto screenIdStr = payload[PAYLOAD_KEY_SCREEN_ID].get<std::string>();
        uint64_t screenId = 0;
        auto result = std::from_chars(screenIdStr.data(),
            screenIdStr.data() + screenIdStr.size(), screenId);
        if (result.ec == std::errc()) {
            return screenId;
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> SlideRecognizerProxy::ExtractDisplayIdFromPayload(const nlohmann::json& payload)
{
    if (payload.contains(PAYLOAD_KEY_DISPLAY_ID) && payload[PAYLOAD_KEY_DISPLAY_ID].is_string()) {
        auto displayIdStr = payload[PAYLOAD_KEY_DISPLAY_ID].get<std::string>();
        uint64_t displayId = 0;
        auto result = std::from_chars(displayIdStr.data(),
            displayIdStr.data() + displayIdStr.size(), displayId);
        if (result.ec == std::errc()) {
            return displayId;
        }
    }
    return std::nullopt;
}

uint64_t SlideRecognizerProxy::ConvertDisplayIdToRsScreenId(uint64_t displayId)
{
    auto it = displayIdToRsScreenId_.find(displayId);
    if (it != displayIdToRsScreenId_.end()) {
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy ConvertDisplayIdToRsScreenId cache hit"
            " displayId = %{public}llu rsScreenId=%{public}llu", (unsigned long long)displayId,
            (unsigned long long)it->second);
        return it->second;
    }
    OHOS::Rosen::ScreenId rsScreenId = 0;
    bool ret = OHOS::Rosen::DisplayManager::GetInstance().ConvertScreenIdToRsScreenId(
        static_cast<OHOS::Rosen::ScreenId>(displayId), rsScreenId);
    if (!ret) {
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy ConvertDisplayIdToRsScreenId convert failed"
            " displayId = %{public}llu", (unsigned long long)displayId);
        return displayId;
    }
    displayIdToRsScreenId_[displayId] = static_cast<uint64_t>(rsScreenId);
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy ConvertDisplayIdToRsScreenId convert success"
        " displayId = %{public}llu rsScreenId=%{public}llu", (unsigned long long)displayId,
        (unsigned long long)rsScreenId);
    return static_cast<uint64_t>(rsScreenId);
}

void SlideRecognizerProxy::OnDispatchResource(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource"
        " resType=%{public}u value=%{public}lld payload = %{public}s ",
        resType, (long long)value, payload.dump().c_str());
    uint64_t screenId = 0;
    auto screenIdOpt = ExtractScreenIdFromPayload(payload);
    if (screenIdOpt.has_value()) {
        screenId = screenIdOpt.value();
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource"
            " screenId = %{public}llu ", (unsigned long long)screenId);
    } else {
        auto displayIdOpt = ExtractDisplayIdFromPayload(payload);
        if (displayIdOpt.has_value()) {
            uint64_t displayId = displayIdOpt.value();
            if (resType == ResType::RES_TYPE_CLICK_RECOGNIZE &&
                value == ResType::ClickEventType::TOUCH_EVENT_DOWN) {
                screenId = ConvertDisplayIdToRsScreenId(displayId);
                RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource else if"
                    " screenId = %{public}llu ", (unsigned long long)screenId);
            } else {
                auto it = displayIdToRsScreenId_.find(displayId);
                screenId = (it != displayIdToRsScreenId_.end()) ? it->second : displayId;
                RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource else else"
                    " screenId = %{public}llu ", (unsigned long long)screenId);
            }
        }
    }

    auto recognizer = GetOrCreateInstance(screenId);
    if (recognizer == nullptr) {
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource GetOrCreateInstance failed"
            " screenId = %{public}llu ", (unsigned long long)screenId);
        return;
    }
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy OnDispatchResource dispatch to"
        " screenId = %{public}llu ", (unsigned long long)screenId);
    nlohmann::json payloadWithScreenId = payload;
    payloadWithScreenId["screenId"] = std::to_string(screenId);
    recognizer->OnDispatchResource(resType, value, payloadWithScreenId);
}

std::shared_ptr<SlideRecognizer> SlideRecognizerProxy::GetOrCreateInstance(uint64_t screenId)
{
    std::lock_guard<std::mutex> lock(instancesMutex_);
    auto it = instances_.find(screenId);
    if (it != instances_.end()) {
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy GetOrCreateInstance found existing "
            " screenId = %{public}llu ", (unsigned long long)screenId);
        return it->second;
    }
    auto recognizer = std::make_shared<SlideRecognizer>();
    if (recognizer == nullptr) {
        return nullptr;
    }
    recognizer->listFlingTimeOutTime_ = listFlingTimeOutTime_;
    recognizer->listFlingEndTime_ = listFlingEndTime_;
    recognizer->listFlingSpeedLimit_ = listFlingSpeedLimit_;
    instances_[screenId] = recognizer;
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy GetOrCreateInstance created "
        " screenId = %{public}llu total =%{public}zu", (unsigned long long)screenId, instances_.size());
    if (instances_.size() > MAX_INSTANCE_COUNT) {
        CleanupIdleInstances();
    }
    return recognizer;
}

void SlideRecognizerProxy::CleanupIdleInstances()
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy CleanupIdleInstances cleanup total =%{public}zu",
        instances_.size());
    for (auto it = instances_.begin(); it != instances_.end();) {
        if (it->second && it->second->GetSlideStatus() == SlideRecognizeStat::IDLE &&
            it->second->IsTaskClean()) {
                RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy CleanupIdleInstances"
                    " screenId = %{public}llu", (unsigned long long)it->first);
                it = instances_.erase(it);
        } else {
            ++it;
        }
    }
}

void SlideRecognizerProxy::SetListFlingTimeoutTime(int64_t value)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy SetListFlingTimeoutTime value =%{public}lld",
        (long long)value);
    listFlingTimeOutTime_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingTimeOutTime_ = value;
    }
}

void SlideRecognizerProxy::SetListFlingEndTime(int64_t value)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy SetListFlingEndTime value =%{public}lld",
        (long long)value);
    listFlingEndTime_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingEndTime_ = value;
    }
}

void SlideRecognizerProxy::SetListFlingSpeedLimit(float value)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizerProxy SetListFlingSpeedLimit value =%{public}f", value);
    listFlingSpeedLimit_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingSpeedLimit_ = value;
    }
}

} // namespace ResourceSchedule
} // namespace OHOS
