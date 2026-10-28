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
#include "slide_recognizer.h"

#include <charconv>
#include <cstdint>
#include <memory>
#include <mutex>

#include "event_listener_mgr.h"
#include "ffrt_inner.h"
#include "hitrace_meter.h"
#include "plugin_mgr.h"
#include "res_common_util.h"
#include "res_sched_log.h"
#include "res_sched_mgr.h"
#include "res_type.h"
#include "scene_recognizer_mgr.h"

namespace OHOS {
namespace ResourceSchedule {
namespace {
    static const uint32_t AXIS_EVENT_FACTOR = 10;
    static const char* AXIS_EVENT_TYPE = "axis_event_type";
    static const char* UP_SPEED_KEY = "up_speed";
}

SlideRecognizer::SlideRecognizer()
{
}

SlideRecognizer::~SlideRecognizer()
{
    RESSCHED_LOGI("~UpdatingSceneRecognizer");
}

void SlideRecognizer::OnDispatchResource(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer OnDispatchResource resType=%{public}u value=%{public}lld"
        " payload = %{public}s ", resType, (long long)value, payload.dump().c_str());
    switch (resType) {
        case ResType::RES_TYPE_SLIDE_RECOGNIZE:
            HandleSlideEvent(value, payload);
            break;
        case ResType::RES_TYPE_SEND_FRAME_EVENT:
            HandleSendFrameEvent(FillRealPidAndUid(payload));
            break;
        case ResType::RES_TYPE_CLICK_RECOGNIZE:
            HandleClickEvent(value, payload);
            break;
        case ResType::RES_TYPE_AXIS_EVENT:
            if (value == ResType::AxisEventStatus::AXIS_EVENT_END) {
                HandleClickEvent(ResType::ClickEventType::TOUCH_EVENT_UP, payload);
            }
            break;
        default:
            RESSCHED_LOGD("unknown resType");
            break;
    }
}

void SlideRecognizer::HandleSlideEvent(int64_t value, const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSlideEvent value=%{public}lld"
        " slideState==%{public}u payload = %{public}s ", (long long)value, slideState_, payload.dump().c_str());
    if (value == ResType::SlideEventStatus::SLIDE_EVENT_DETECTING) {
        HandleSlideDetecting(payload);
    } else if (value == ResType::SlideEventStatus::SLIDE_EVENT_ON) {
        HandleListFlingStart(payload);
    } else if (value == ResType::SlideEventStatus::SLIDE_EVENT_OFF) {
        HandleSlideOFFEvent();
    }
}

void SlideRecognizer::HandleSlideOFFEvent()
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSlideOFFEvent slideState==%{public}u",
        slideState_);
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    if (listFlingEndTask_) {
        ffrt::skip(listFlingEndTask_);
    }
    if (listFlingTimeOutTask_) {
        ffrt::skip(listFlingTimeOutTask_);
    }
    nlohmann::json extInfo;
    // RS side registers the frame report context by screenId only (graphic_2d
    // ressched_event_listener), keep the STOP key identical to the START key
    extInfo["screenId"] = std::to_string(lastScreenId_);
    EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo);
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSlideOFFEvent SendEvent eventType==%{public}u"
        " eventValue=%{public}u extInfo=%{public}s", ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo.dump().c_str());
    slideState_ = SlideRecognizeStat::IDLE;
}

void SlideRecognizer::HandleSlideDetecting(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSlideDetecting slideState==%{public}u"
        " payload = %{public}s ", slideState_, payload.dump().c_str());
    {
        std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
        isInTouching_ = true;
    }
    auto self = shared_from_this();
    SceneRecognizerMgr::GetInstance().SubmitTask([self, payload]() {
        self->StartDetecting(payload);
    });
}

void SlideRecognizer::StartDetecting(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer StartDetecting slideState==%{public}u"
        " payload = %{public}s ", slideState_, payload.dump().c_str());
    auto extInfo = BuildExtInfoWithScreenId(payload);
    if (extInfo.contains("screenId") && extInfo["screenId"].is_string()) {
        auto screenIdStr = extInfo["screenId"].get<std::string>();
        uint64_t screenId = 0;
        auto result = std::from_chars(screenIdStr.data(),
            screenIdStr.data() + screenIdStr.size(), screenId);
        if (result.ec == std::errc()) {
            lastScreenId_ = screenId;
        }
    }
    EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START, extInfo);
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer StartDetecting SendEvent eventType==%{public}u"
        " eventValue=%{public}u extInfo = %{public}s ",
        ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START, extInfo.dump().c_str());
    slideDetectingTime_ = ResCommonUtil::GetNowMillTime(true);
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    slideState_ = SlideRecognizeStat::SLIDE_NORMAL_DETECTING;
    if (!payload.contains("clientPid") || !payload["clientPid"].is_string()) {
        RESSCHED_LOGE("payload with no clientPid");
        return;
    }
    slidePid_ = payload["clientPid"];
    if (!payload.contains("callingUid") || !payload["callingUid"].is_string()) {
        RESSCHED_LOGE("payload with no callingUid");
        return;
    }
    slideUid_ = payload["callingUid"];
    if (payload.contains("scrTid") && payload["scrTid"].is_string()) {
        scrTid_ = payload["scrTid"];
    }
}

void SlideRecognizer::HandleListFlingStart(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleListFlingStart slideState==%{public}u"
        " payload = %{public}s ", slideState_, payload.dump().c_str());
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    auto extInfo = BuildExtInfoWithScreenId(payload);
    EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START, extInfo);
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleListFlingStart SendEvent eventType==%{public}u"
        " eventValue=%{public}u extInfo = %{public}s listFlingEndTime=%{public}lld listFlingTimeOutTime=%{public}lld",
        ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START, extInfo.dump().c_str(),
        (long long)listFlingEndTime_, (long long)listFlingTimeOutTime_);
    if (listFlingEndTask_) {
        ffrt::skip(listFlingEndTask_);
    }
    auto self = shared_from_this();
    listFlingEndTask_ = ffrt::submit_h([self, payload]() {
        self->ReportListFlingLockedEnd(payload);
        }, {}, {}, ffrt::task_attr().delay(listFlingEndTime_));
    if (listFlingTimeOutTask_) {
        ffrt::skip(listFlingTimeOutTask_);
    }
    listFlingTimeOutTask_ = ffrt::submit_h([self, payload]() {
        self->ReportListFlingLockedEnd(payload);
        }, {}, {}, ffrt::task_attr().delay(listFlingTimeOutTime_));
}

void SlideRecognizer::HandleSendFrameEvent(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSendFrameEvent slideState==%{public}u"
        " isInTouching=%{public}d payload = %{public}s ",
        slideState_, isInTouching_, payload.dump().c_str());
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    if (slideState_ == SlideRecognizeStat::SLIDE_NORMAL_DETECTING) {
        if (isInTouching_) {
            ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_SLIDE_RECOGNIZE,
                ResType::SlideEventStatus::SLIDE_NORMAL_BEGIN, payload);
            RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSendFrameEvent report resType==%{public}u"
                " value=%{public}lld payload = %{public}s ",
                ResType::RES_TYPE_SLIDE_RECOGNIZE,
                (long long)ResType::SlideEventStatus::SLIDE_NORMAL_BEGIN, payload.dump().c_str());
            slideState_ = SlideRecognizeStat::SLIDE_NORMAL;
        } else {
            slideState_ = SlideRecognizeStat::IDLE;
        }
        auto extInfo = BuildExtInfoWithScreenId(payload);
        EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
            ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo);
        RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleSendFrameEvent SendEvent eventType==%{public}u"
            " eventValue=%{public}u extInfo = %{public}s ",
            ResType::EventType::EVENT_DRAW_FRAME_REPORT,
            ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo.dump().c_str());
    } else if (slideState_ == SlideRecognizeStat::LIST_FLING) {
        if (listFlingEndTask_) {
            ffrt::skip(listFlingEndTask_);
        }
        auto self = shared_from_this();
        listFlingEndTask_ = ffrt::submit_h([self, payload]() {
            self->ReportListFlingLockedEnd(payload);
            }, {}, {}, ffrt::task_attr().delay(listFlingEndTime_));
    }
}

void SlideRecognizer::ListFlingEnd(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer ListFlingEnd slideState==%{public}u ",
        slideState_);
    if (slideState_ != SlideRecognizeStat::LIST_FLING) {
        return;
    }
    if (listFlingEndTask_) {
        ffrt::skip(listFlingEndTask_);
    }
    if (listFlingTimeOutTask_) {
        ffrt::skip(listFlingTimeOutTask_);
    }
    listFlingEndTask_ = nullptr;
    listFlingTimeOutTask_ = nullptr;
    ReportListFlingLockedEnd(FillRealPidAndUid(payload));
}

void SlideRecognizer::HandleClickEvent(int64_t value, const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleClickEvent value==%{public}lld"
        " slideState==%{public}u payload=%{public}s", (long long)value, slideState_, payload.dump().c_str());
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    if (value == ResType::ClickEventType::TOUCH_EVENT_DOWN) {
        ListFlingEnd(payload);
        slideState_ = SlideRecognizeStat::IDLE;
        isInTouching_ = true;
    } else if (value == ResType::ClickEventType::TOUCH_EVENT_UP ||
        value == ResType::ClickEventType::TOUCH_EVENT_PULL_UP) {
        isInTouching_ = false;
    }
    //not in slide stat or slide detecting stat
    if (slideState_ != SlideRecognizeStat::SLIDE_NORMAL &&
        slideState_ != SlideRecognizeStat::SLIDE_NORMAL_DETECTING) {
        return;
    }
    // receive up event, slide normal end
    if (value == ResType::ClickEventType::TOUCH_EVENT_UP ||
        value == ResType::ClickEventType::TOUCH_EVENT_PULL_UP) {
        if (slideState_ != SlideRecognizeStat::SLIDE_NORMAL_DETECTING) {
            ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_SLIDE_RECOGNIZE,
                ResType::SlideEventStatus::SLIDE_NORMAL_END, FillRealPidAndUid(payload));
            RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleClickEvent report resType==%{public}u"
                " value=%{public}lld",ResType::RES_TYPE_SLIDE_RECOGNIZE,
                (long long)ResType::SlideEventStatus::SLIDE_NORMAL_END);
        }
        float upSpeed = 0.0;
        if (!payload.contains("clientPid") || !payload["clientPid"].is_string()) {
            RESSCHED_LOGE("payload with no clientPid");
            return;
        }
        if (!payload.contains(UP_SPEED_KEY) || !payload[UP_SPEED_KEY].is_string()) {
            return;
        }
        if (!ResCommonUtil::StrToFloat(payload[UP_SPEED_KEY], upSpeed)) {
            return;
        }
        std::string trace_str("TOUCH EVENT UPSPEED: ");
        trace_str.append(std::to_string(upSpeed));
        StartTrace(HITRACE_TAG_APP, trace_str, -1);
        FinishTrace(HITRACE_TAG_APP);
        if (payload.contains(AXIS_EVENT_TYPE) && payload[AXIS_EVENT_TYPE].is_string() &&
            payload[AXIS_EVENT_TYPE] == AXIS_EVENT_TYPE) {
            upSpeed = upSpeed * AXIS_EVENT_FACTOR;
        }
        // if up speed large than LIST_FLING_SPEED_LIMIT,start recognize list fling.
        if (upSpeed > listFlingSpeedLimit_) {
            auto extInfo = BuildExtInfoWithScreenId(payload);
            EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
                ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START, extInfo);
            RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer HandleClickEvent SendEvent eventType==%{public}u"
                " eventValue=%{public}u extInfo=%{public}s upSpeed=%{public}f limit=%{public}f",
                ResType::EventType::EVENT_DRAW_FRAME_REPORT,
                ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_START,extInfo.dump().c_str(),
                upSpeed, listFlingSpeedLimit_);
            ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_SLIDE_RECOGNIZE,
                ResType::SlideEventStatus::SLIDE_EVENT_ON, FillRealPidAndUid(payload));
            slideState_ = SlideRecognizeStat::LIST_FLING;
        }
    }
}

nlohmann::json SlideRecognizer::FillRealPidAndUid(const nlohmann::json& payload)
{
    nlohmann::json payloadM = payload;
    if (!slidePid_.empty()) {
        payloadM["clientPid"] = slidePid_;
    }
    if (!slideUid_.empty()) {
        payloadM["callingUid"] = slideUid_;
    }
    payloadM["scrTid"] = scrTid_;
    return payloadM;
}

nlohmann::json SlideRecognizer::BuildExtInfoWithScreenId(const nlohmann::json& payload)
{
    // RS side registers the frame report context by screenId only (graphic_2d
    // ressched_event_listener); pid is not part of the registration key
    nlohmann::json extInfo;
    if (payload.contains("screenId") && payload["screenId"].is_string()) {
        extInfo["screenId"] = payload["screenId"];
    } else {
        extInfo["screenId"] = std::to_string(lastScreenId_);
    }
    return extInfo;
}

void SlideRecognizer::ReportListFlingLockedEnd(const nlohmann::json& payload)
{
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer ReportListFlingLockedEnd slideState==%{public}u "
        "payload=%{public}s", slideState_, payload.dump().c_str());
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    if (slideState_ != SlideRecognizeStat::LIST_FLING) {
        return;
    }
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer ReportListFlingLockedEnd report SLIDE_EVENT_OFF"
        " resType=%{public}u value=%{public}lld", ResType::RES_TYPE_SLIDE_RECOGNIZE,
        (long long)ResType::SlideEventStatus::SLIDE_EVENT_OFF);
    ResSchedMgr::GetInstance().ReportData(ResType::RES_TYPE_SLIDE_RECOGNIZE,
        ResType::SlideEventStatus::SLIDE_EVENT_OFF, payload);
    auto extInfo = BuildExtInfoWithScreenId(payload);
    EventListenerMgr::GetInstance().SendEvent(ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo);
    RESSCHED_LOGI("slide reco multiprocessing SlideRecognizer ReportListFlingLockedEnd SendEvent eventType=%{public}u"
        " eventValue=%{public}u extInfo=%{public}s", ResType::EventType::EVENT_DRAW_FRAME_REPORT,
        ResType::EventValue::EVENT_VALUE_DRAW_FRAME_REPORT_STOP, extInfo.dump().c_str());
    slideState_ = SlideRecognizeStat::IDLE;
}

bool SlideRecognizer::IsTaskClean() const
{
    return listFlingEndTask_ == nullptr && listFlingTimeOutTask_ == nullptr;
}

uint32_t SlideRecognizer::GetSlideStatus()
{
    std::lock_guard<ffrt::recursive_mutex> lock(stateMutex_);
    return slideState_;
}
} // namespace ResourceSchedule
} // namespace OHOS
