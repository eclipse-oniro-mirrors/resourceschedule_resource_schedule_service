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
#include "hitrace_meter.h"
#include "res_common_util.h"
#include "res_sched_log.h"
#include "res_type.h"

#include <charconv>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
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

std::optional<uint64_t> SlideRecognizerProxy::ExtractIdFromPayload(const nlohmann::json& payload,
    const char* key)
{
    if (payload.contains(key) && payload[key].is_string()) {
        auto idStr = payload[key].get<std::string>();
        uint64_t id = 0;
        auto result = std::from_chars(idStr.data(), idStr.data() + idStr.size(), id);
        if (result.ec == std::errc()) {
            return id;
        }
    }
    return std::nullopt;
}

uint64_t SlideRecognizerProxy::ConvertDisplayIdToRsScreenId(uint64_t displayId)
{
    {
        std::lock_guard<std::mutex> lock(displayIdMutex_);
        auto it = displayIdToRsScreenId_.find(displayId);
        if (it != displayIdToRsScreenId_.end()) {
            return it->second;
        }
    }
    OHOS::Rosen::ScreenId rsScreenId = 0;
    bool ret = OHOS::Rosen::DisplayManager::GetInstance().ConvertScreenIdToRsScreenId(
        static_cast<OHOS::Rosen::ScreenId>(displayId), rsScreenId);
    if (!ret) {
        return DEFAULT_SCREEN_ID;
    }
    {
        std::lock_guard<std::mutex> lock(displayIdMutex_);
        displayIdToRsScreenId_[displayId] = static_cast<uint64_t>(rsScreenId);
    }
    return static_cast<uint64_t>(rsScreenId);
}

void SlideRecognizerProxy::OnDispatchResource(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    std::string trace_str("SlideRecognizerProxy:OnDispatchResource resType:");
    trace_str.append(std::to_string(resType));
    trace_str.append(" value:");
    trace_str.append(std::to_string(value));
    StartTrace(HITRACE_TAG_APP, trace_str, -1);
    uint64_t screenId = DEFAULT_SCREEN_ID;
    auto screenIdOpt = ExtractIdFromPayload(payload, PAYLOAD_KEY_SCREEN_ID);
    auto displayIdOpt = ExtractIdFromPayload(payload, PAYLOAD_KEY_DISPLAY_ID);
    if (screenIdOpt.has_value()) {
        screenId = screenIdOpt.value();
    } else if (displayIdOpt.has_value() && resType == ResType::RES_TYPE_CLICK_RECOGNIZE &&
        value == ResType::ClickEventType::TOUCH_EVENT_DOWN) {
        screenId = ConvertDisplayIdToRsScreenId(displayIdOpt.value());
    } else if (displayIdOpt.has_value()) {
        std::lock_guard<std::mutex> lock(displayIdMutex_);
        auto it = displayIdToRsScreenId_.find(displayIdOpt.value());
        screenId = (it != displayIdToRsScreenId_.end()) ? it->second : DEFAULT_SCREEN_ID;
    }
    auto recognizer = GetOrCreateInstance(screenId);
    std::string screen_id_trace_str("GetOrCreateInstance screenId:");
    screen_id_trace_str.append(std::to_string(screenId));
    StartTrace(HITRACE_TAG_APP, screen_id_trace_str, -1);
    FinishTrace(HITRACE_TAG_APP);
    nlohmann::json payloadWithScreenId = payload;
    payloadWithScreenId["screenId"] = std::to_string(screenId);
    recognizer->OnDispatchResource(resType, value, payloadWithScreenId);
    FinishTrace(HITRACE_TAG_APP);
}

std::shared_ptr<SlideRecognizer> SlideRecognizerProxy::GetOrCreateInstance(uint64_t screenId)
{
    std::lock_guard<std::mutex> lock(instancesMutex_);
    auto it = instances_.find(screenId);
    if (it != instances_.end()) {
        return it->second;
    }
    auto recognizer = std::make_shared<SlideRecognizer>();
    recognizer->listFlingTimeOutTime_ = listFlingTimeOutTime_;
    recognizer->listFlingEndTime_ = listFlingEndTime_;
    recognizer->listFlingSpeedLimit_ = listFlingSpeedLimit_;
    instances_[screenId] = recognizer;
    if (instances_.size() > MAX_INSTANCE_COUNT) {
        CleanupIdleInstances();
    }
    return recognizer;
}

void SlideRecognizerProxy::CleanupIdleInstances()
{
    for (auto it = instances_.begin(); it != instances_.end();) {
        if (it->second && it->second->GetSlideStatus() == SlideRecognizeStat::IDLE &&
            it->second->IsTaskClean()) {
            it = instances_.erase(it);
        } else {
            ++it;
        }
    }
}

void SlideRecognizerProxy::SetListFlingTimeoutTime(int64_t value)
{
    listFlingTimeOutTime_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingTimeOutTime_ = value;
    }
}

void SlideRecognizerProxy::SetListFlingEndTime(int64_t value)
{
    listFlingEndTime_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingEndTime_ = value;
    }
}

void SlideRecognizerProxy::SetListFlingSpeedLimit(float value)
{
    listFlingSpeedLimit_ = value;
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [screenId, recognizer] : instances_) {
        recognizer->listFlingSpeedLimit_ = value;
    }
}

} // namespace ResourceSchedule
} // namespace OHOS
