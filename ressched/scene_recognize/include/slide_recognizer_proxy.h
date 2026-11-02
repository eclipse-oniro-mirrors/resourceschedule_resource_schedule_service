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

#ifndef RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_PROXY_H
#define RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_PROXY_H

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include "scene_recognizer_base.h"

namespace OHOS {
namespace ResourceSchedule {

namespace SlideRecognizeStat {
enum : uint32_t {
    IDLE,
    SLIDE_NORMAL_DETECTING,
    SLIDE_NORMAL,
    LIST_FLING,
};
}

static constexpr int64_t DEFAULT_LIST_FLING_TIME_OUT_TIME = 3 * 1000 * 1000;
static constexpr int64_t DEFAULT_LIST_FLING_END_TIME = 150 * 1000;
static constexpr float DEFAULT_LIST_FLING_SPEED_LIMIT = 3.0;
static constexpr uint64_t MAIN_SCREEN_ID = 0;

class SlideRecognizer;

class SlideRecognizerProxy : public SceneRecognizerBase {
public:
    SlideRecognizerProxy();
    ~SlideRecognizerProxy();

    void OnDispatchResource(uint32_t resType, int64_t value, const nlohmann::json& payload) override;
    void SetListFlingTimeoutTime(int64_t value);
    void SetListFlingEndTime(int64_t value);
    void SetListFlingSpeedLimit(float value);

protected:
    int64_t listFlingTimeOutTime_ = DEFAULT_LIST_FLING_TIME_OUT_TIME;
    int64_t listFlingEndTime_ = DEFAULT_LIST_FLING_END_TIME;
    float listFlingSpeedLimit_ = DEFAULT_LIST_FLING_SPEED_LIMIT;

private:
    std::shared_ptr<SlideRecognizer> GetOrCreateInstance(uint64_t screenId);
    void CleanupIdleInstances();
    std::optional<uint64_t> ExtractScreenIdFromPayload(const nlohmann::json& payload);
    std::optional<uint64_t> ExtractDisplayIdFromPayload(const nlohmann::json& payload);
    uint64_t ConvertDisplayIdToRsScreenId(uint64_t displayId);

    std::unordered_map<uint64_t, std::shared_ptr<SlideRecognizer>> instances_;
    std::mutex instancesMutex_;
    static constexpr size_t MAX_INSTANCE_COUNT = 10;
    std::unordered_map<uint64_t, uint64_t> displayIdToRsScreenId_;
    std::mutex displayIdMutex_;
};

} // namespace ResourceSchedule
} // namespace OHOS
#endif // RESSCHED_SCENE_RECOGNIZE_SLIDE_RECOGNIZER_PROXY_H
