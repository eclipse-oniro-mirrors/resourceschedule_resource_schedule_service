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

#ifndef RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_H
#define RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_H

#include "slide_recognizer_proxy.h"
#include "ffrt.h"
#include <cstdint>
#include <memory>

namespace OHOS {
namespace ResourceSchedule {
class SlideRecognizer : public SlideRecognizerProxy,
    public std::enable_shared_from_this<SlideRecognizer> {
public:
    SlideRecognizer();
    ~SlideRecognizer();
    void OnDispatchResource(uint32_t, int64_t value, const nlohmann::json& payload) override;
    bool IsTaskClean() const;
    uint32_t GetSlideStatus();
    void ReportListFlingLockedEnd(const nlohmann::json& payload);
private:
    void HandleSlideEvent(int64_t value, const nlohmann::json& payload);
    void HandleSendFrameEvent(const nlohmann::json& payload);
    void HandleClickEvent(int64_t value, const nlohmann::json& payload);
    void HandleSlideDetecting(const nlohmann::json& payload);
    void HandleListFlingStart(const nlohmann::json& payload);
    void HandleSlideOFFEvent(const nlohmann::json& payload);
    void StartDetecting(const nlohmann::json& payload);
    nlohmann::json FillRealPidAndUid(const nlohmann::json& payload);
    void ListFlingEnd(const nlohmann::json& payload);
    nlohmann::json BuildExtInfoWithScreenId(const nlohmann::json& payload);
    uint32_t slideState_ = SlideRecognizeStat::IDLE;
    ffrt::recursive_mutex stateMutex_;
    ffrt::task_handle listFlingEndTask_ = nullptr;
    ffrt::task_handle listFlingTimeOutTask_ = nullptr;
    int64_t slideDetectingTime_ = -1;
    bool isInTouching_ = false;
    std::string slidePid_ = "";
    std::string slideUid_ = "";
    std::string scrTid_ = "";
};
} // namespace ResourceSchedule
} // namespace OHOS
#endif // RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_H
