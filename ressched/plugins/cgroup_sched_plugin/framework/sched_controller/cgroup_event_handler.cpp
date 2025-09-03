/*
 * Copyright (c) 2022-2023 Huawei Device Co., Ltd.
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

#include "cgroup_event_handler.h"
#include <cinttypes>
#include "ability_info.h"
#include "app_mgr_constants.h"
#include "cgroup_adjuster.h"
#include "cgroup_sched_log.h"
#include "hisysevent.h"
#include "ressched_utils.h"
#include "res_type.h"
#include "sched_controller.h"
#include "sched_policy.h"
#include "system_ability_definition.h"
#ifdef POWER_MANAGER_ENABLE
#include "power_mgr_client.h"
#endif

#undef LOG_TAG
#define LOG_TAG "CgroupEventHandler"

namespace OHOS {
namespace ResourceSchedule {
namespace {
    constexpr uint32_t EVENT_ID_REG_APP_STATE_OBSERVER = 1;
    constexpr uint32_t EVENT_ID_REG_BGTASK_OBSERVER = 2;
    constexpr uint32_t DELAYED_RETRY_REGISTER_DURATION = 100;
    constexpr uint32_t MAX_RETRY_TIMES = 100;
    constexpr uint32_t MAX_SPAN_SERIAL = 99;
    constexpr uint32_t MAX_AUDIO_PLATING_COUNT = 1024;
    const std::string MMI_SERVICE_NAME = "mmi_service";
}

using OHOS::AppExecFwk::ApplicationState;
using OHOS::AppExecFwk::AbilityState;
using OHOS::AppExecFwk::AbilityType;
using OHOS::AppExecFwk::ExtensionState;
using OHOS::AppExecFwk::ProcessType;

CgroupEventHandler::CgroupEventHandler(const std::string &queueName)
{
    cgroupEventQueue_ = std::make_shared<ffrt::queue>(queueName.c_str(),
        ffrt::queue_attr().qos(ffrt::qos_user_interactive));
    if (!cgroupEventQueue_) {
        CGS_LOGE("%{public}s : create cgroupEventQueue_ failed", __func__);
    }
}

CgroupEventHandler::~CgroupEventHandler()
{
    supervisor_ = nullptr;
    cgroupEventQueue_ = nullptr;
    delayTaskMap_.clear();
}

void CgroupEventHandler::ProcessEvent(uint32_t eventId, int64_t eventParam)
{
    CGS_LOGD("%{public}s : eventId:%{public}d param:%{public}" PRIu64,
        __func__, eventId, eventParam);
}

void CgroupEventHandler::SetSupervisor(std::shared_ptr<Supervisor> supervisor)
{
    supervisor_ = supervisor;
}

void CgroupEventHandler::HandleAbilityAdded(int32_t saId, const std::string& deviceId)
{
    switch (saId) {
        case APP_MGR_SERVICE_ID:
            if (supervisor_ != nullptr) {
                supervisor_->InitSuperVisorContent();
            }
            break;
#ifdef POWER_MANAGER_ENABLE
        case POWER_MANAGER_SERVICE_ID:
            SchedController::GetInstance().GetRunningLockState();
            break;
#endif
        default:
            break;
    }
}

void CgroupEventHandler::HandleAbilityRemoved(int32_t saId, const std::string& deviceId)
{
    CGS_LOGD("%{public}s : saId:%{public}d", __func__, saId);
}

void CgroupEventHandler::HandleApplicationStateChanged(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    int32_t state = 0;

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload) || !ParseValue(state, "state", payload)) {
        CGS_LOGD("%{public}s: param error", __func__);
        return;
    }

    CGS_LOGD("%{public}s : %{public}d, %{public}s, %{public}d", __func__, uid, bundleName.c_str(), state);
    ChronoScope cs("HandleApplicationStateChanged");
    if (state == (int32_t)(ApplicationState::APP_STATE_TERMINATED)) {
        return;
    }
    std::shared_ptr<Application> app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(bundleName);
    app->state_ = state;
}

void CgroupEventHandler::HandleProcessStateChanged(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    int32_t state = 0;
    if (!ParseValue(uid, "uid", payload) ||
        !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload) ||
        !ParseValue(state, "state", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s, %{public}d", __func__, uid,
        pid, bundleName.c_str(), state);
    ChronoScope cs("HandleProcessStateChanged");
    std::shared_ptr<Application> app = supervisor_->GetAppRecordNonNull(uid);
    std::shared_ptr<ProcessRecord> procRecord = app->GetProcessRecordNonNull(pid);
    procRecord->processState_ = state;
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_PROCESS_STATE);
}

void CgroupEventHandler::HandleUIExtensionAbilityStateChange(uint32_t resType, int64_t value,
    const nlohmann::json& payload)
{
    int32_t extensionAbilityType = 0;
    int32_t uiExtensionState = 0;
    if (!ParseValue(extensionAbilityType, "extensionAbilityType", payload) ||
        !ParseValue(uiExtensionState, "uiExtensionState", payload)) {
        CGS_LOGD("%{public}s : this type of event is not dealt with here", __func__);
        return;
    }

    nlohmann::json payloadChange = payload;
    payloadChange["extensionState"] = std::to_string(uiExtensionState);
    HandleExtensionStateChanged(resType, value, payloadChange);
}

void CgroupEventHandler::HandleAbilityStateChanged(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        return;
    }

    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    std::string abilityName;
    int32_t recordId = 0;
    int32_t abilityState = 0;
    int32_t abilityType = 0;
    int32_t callerUid = -1;
    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload) || !ParseString(abilityName, "abilityName", payload) ||
        !ParseValue(recordId, "recordId", payload) || !ParseValue(callerUid, "callerUid", payload) ||
        !ParseValue(abilityState, "abilityState", payload) || !ParseValue(abilityType, "abilityType", payload)) {
        return;
    }

    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s, %{public}s, %{public}d, %{public}d, %{public}d",
        __func__, uid, pid, bundleName.c_str(), abilityName.c_str(), recordId, abilityState, abilityType);
    if (abilityType == (int32_t)AbilityType::EXTENSION) {
        HandleUIExtensionAbilityStateChange(resType, value, payload);
        return;
    }
    ChronoScope cs("HandleAbilityStateChanged");
    if (abilityState == (int32_t)(AbilityState::ABILITY_STATE_TERMINATED)) {
        auto app = supervisor_->GetAppRecord(uid);
        if (app) {
            auto procRecord = app->GetProcessRecord(pid);
            if (procRecord) {
                procRecord->RemoveAbilityByRecordId(recordId);
                CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
                    AdjustSource::ADJS_ABILITY_STATE);
            }
        }
        return;
    }
    auto app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(bundleName);
    auto procRecord = app->GetProcessRecordNonNull(pid);
    auto abiInfo = procRecord->GetAbilityInfoNonNull(recordId);
    abiInfo->name_ = abilityName;
    abiInfo->state_ = abilityState;
    abiInfo->type_ = abilityType;
    if (abilityState == 0) {
        ResSchedUtils::GetInstance().ReportCallerEvent(*(app.get()), *(procRecord.get()), callerUid);
    }
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_ABILITY_STATE);
}

void CgroupEventHandler::HandleExtensionStateChanged(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    std::string abilityName;
    int32_t recordId = 0;
    int32_t extensionState = 0;
    int32_t abilityType = 0;
    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload) || !ParseString(abilityName, "abilityName", payload) ||
        !ParseValue(recordId, "recordId", payload) ||
        !ParseValue(extensionState, "extensionState", payload) || !ParseValue(abilityType, "abilityType", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }
    
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s, %{public}s, %{public}d, %{public}d, %{public}d",
        __func__, uid, pid, bundleName.c_str(), abilityName.c_str(), recordId, extensionState, abilityType);
    ChronoScope cs("HandleExtensionStateChanged");
    if (extensionState == (int32_t)(ExtensionState::EXTENSION_STATE_TERMINATED)) {
        auto app = supervisor_->GetAppRecord(uid);
        if (app) {
            auto procRecord = app->GetProcessRecord(pid);
            if (procRecord) {
                procRecord->RemoveAbilityByRecordId(recordId);
                CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
                    AdjustSource::ADJS_EXTENSION_STATE);
            }
        }
        return;
    }
    auto app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(bundleName);
    auto procRecord = app->GetProcessRecordNonNull(pid);
    auto abiInfo = procRecord->GetAbilityInfoNonNull(recordId);
    abiInfo->name_ = abilityName;
    abiInfo->estate_ = extensionState;
    abiInfo->type_ = abilityType;
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_EXTENSION_STATE);
}

void CgroupEventHandler::HandleProcessStateChangedEx(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    if (value == ResType::ProcessStatus::PROCESS_CREATED) {
        HandleProcessCreated(resType, value, payload);
    } else if (value == ResType::ProcessStatus::PROCESS_DIED) {
        HandleProcessDied(resType, value, payload);
    } else {
        HandleProcessStateChanged(resType, value, payload);
    }
}

void CgroupEventHandler::HandleProcessCreated(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t hostPid = 0;
    std::string bundleName;
    int32_t extensionType = 0;
    int32_t processType = 0;
    int32_t isPreloadModule = 0;
    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload) || !ParseValue(hostPid, "hostPid", payload) ||
        !ParseValue(extensionType, "extensionType", payload) || !ParseValue(processType, "processType", payload) ||
        !ParseValue(isPreloadModule, "isPreloadModule", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }
    CGS_LOGI("%{public}s : %{public}d, %{public}d, %{public}d, %{public}d, %{public}s, %{public}d",
        __func__, uid, pid, hostPid, processType, bundleName.c_str(),
        extensionType);
    ChronoScope cs("HandleProcessCreated");
    std::shared_ptr<Application> app = supervisor_->GetAppRecordNonNull(uid);
    std::shared_ptr<ProcessRecord> procRecord = app->GetProcessRecordNonNull(pid);
    app->SetName(bundleName);
    std::string processName;
    if (ParseString(processName, "processName", payload)) {
        procRecord->SetName(processName);
    } else {
        CGS_LOGE("%{public}s: param error,not have processName", __func__);
    }
    procRecord->processType_ = processType < ProcRecordType::PROC_RECORD_TYPE_MAX ?
        processType : ProcRecordType::NORMAL;
    switch (processType) {
        case static_cast<int32_t>(ProcessType::RENDER):
        case static_cast<int32_t>(ProcessType::CHILD):
        case static_cast<int32_t>(ProcessType::GPU):
            procRecord->hostPid_ = hostPid;
            app->AddHostProcess(hostPid);
            break;
        case static_cast<int32_t>(ProcessType::EXTENSION):
            procRecord->extensionType_ = extensionType;
            break;
        default:
            break;
    }
    AdjustSource policy = (isPreloadModule != 0) ? AdjustSource::ADJS_APP_PRELOAD : AdjustSource::ADJS_PROCESS_CREATE;
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()), policy);
}

void CgroupEventHandler::HandleProcessDied(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    if (!ParseValue(uid, "uid", payload) ||
        !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload)) {
        return;
    }
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s", __func__, uid, pid, bundleName.c_str());
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    if (!app) {
        CGS_LOGE("%{public}s : application %{public}s not exist!", __func__, bundleName.c_str());
        return;
    }
    std::shared_ptr<ProcessRecord> procRecord = app->GetProcessRecord(pid);
    if (procRecord) {
        ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
            ResType::RES_TYPE_PROCESS_STATE_CHANGE, ResType::ProcessStatus::PROCESS_DIED);
    }
    app->RemoveProcessRecord(pid);
    // if all processes died, remove current app
    if (app->GetPidsMap().size() == 0) {
        supervisor_->RemoveApplication(uid);
    }
}

void CgroupEventHandler::HandleTransientTaskStatus(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    std::string bundleName;
    if (!ParseValue(uid, "uid", payload) ||
        !ParseValue(pid, "pid", payload) ||
        !ParseString(bundleName, "bundleName", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }

    if (value == ResType::TransientTaskStatus::TRANSIENT_TASK_START) {
        HandleTransientTaskStart(uid, pid, bundleName);
    } else if (value == ResType::TransientTaskStatus::TRANSIENT_TASK_END) {
        HandleTransientTaskEnd(uid, pid, bundleName);
    }
}

void CgroupEventHandler::HandleTransientTaskStart(uid_t uid, pid_t pid, const std::string& packageName)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s", __func__, uid, pid, packageName.c_str());
    auto app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(packageName);
    auto procRecord = app->GetProcessRecord(pid);
    if (!procRecord) {
        return;
    }
    procRecord->runningTransientTask_ = true;
}

void CgroupEventHandler::HandleTransientTaskEnd(uid_t uid, pid_t pid, const std::string& packageName)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}s", __func__, uid, pid, packageName.c_str());
    auto app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(packageName);
    auto procRecord = app->GetProcessRecord(pid);
    if (!procRecord) {
        return;
    }
    procRecord->runningTransientTask_ = false;
}

void CgroupEventHandler::HandleContinuousTaskStatus(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t abilityId = 0;
    if (!ParseValue(uid, "uid", payload) ||
        !ParseValue(pid, "pid", payload) ||
        !ParseValue(abilityId, "abilityId", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }

    if (value == ResType::ContinuousTaskStatus::CONTINUOUS_TASK_START ||
        value == ResType::ContinuousTaskStatus::CONTINUOUS_TASK_UPDATE) {
        std::vector<uint32_t> typeIds;
        if (payload.contains("typeIds") && payload["typeIds"].is_array()) {
            typeIds = payload["typeIds"].get<std::vector<uint32_t>>();
        }
        HandleContinuousTaskUpdate(uid, pid, typeIds, abilityId);
    } else if (value == ResType::ContinuousTaskStatus::CONTINUOUS_TASK_END) {
        HandleContinuousTaskCancel(uid, pid, abilityId);
    }
}

void CgroupEventHandler::HandleContinuousTaskUpdate(uid_t uid, pid_t pid,
    const std::vector<uint32_t>& typeIds, int32_t abilityId)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    ChronoScope cs("HandleContinuousTaskUpdate");
    auto app = supervisor_->GetAppRecordNonNull(uid);
    auto procRecord = app->GetProcessRecordNonNull(pid);
    procRecord->continuousTaskFlag_ = 0;
    procRecord->abilityIdAndContinuousTaskFlagMap_[abilityId] = typeIds;
    for (const auto& typeId : typeIds) {
        CGS_LOGI("%{public}s : %{public}d, %{public}d, %{public}d, abilityId %{public}d,",
            __func__, uid, pid, typeId, abilityId);
    }
    for (const auto& ablityIdAndcontinuousTaskFlag : procRecord->abilityIdAndContinuousTaskFlagMap_) {
        for (const auto& typeId : ablityIdAndcontinuousTaskFlag.second) {
            procRecord->continuousTaskFlag_ |= (1U << typeId);
        }
    }
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_CONTINUOUS_BEGIN);
}

void CgroupEventHandler::HandleContinuousTaskCancel(uid_t uid, pid_t pid, int32_t abilityId)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    CGS_LOGI("%{public}s : %{public}d, %{public}d, %{public}d", __func__, uid, pid, abilityId);
    ChronoScope cs("HandleContinuousTaskCancel");
    auto app = supervisor_->GetAppRecordNonNull(uid);
    auto procRecord = app->GetProcessRecord(pid);
    if (!procRecord) {
        return;
    }
    procRecord->abilityIdAndContinuousTaskFlagMap_.erase(abilityId);
    procRecord->continuousTaskFlag_ = 0;
    for (const auto& ablityIdAndcontinuousTaskFlag : procRecord->abilityIdAndContinuousTaskFlagMap_) {
        for (const auto& typeId : ablityIdAndcontinuousTaskFlag.second) {
            procRecord->continuousTaskFlag_ |= (1U << typeId);
        }
    }
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_CONTINUOUS_END);
}

void CgroupEventHandler::HandleFocusStateChange(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    int32_t windowId = 0;
    int32_t windowType = 0;
    int64_t displayId = 0;
    int32_t pid = 0;
    int32_t uid = 0;

    if (!ParseValue(pid, "pid", payload) || !ParseValue(uid, "uid", payload) ||
        !ParseValue(windowId, "windowId", payload) || !ParseValue(windowType, "windowType", payload) ||
        !ParseLongValue(displayId, "displayId", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }

    if (value == ResType::WindowFocusStatus::WINDOW_FOCUS) {
        HandleFocusedWindow(windowId, windowType, displayId, pid, uid);
    } else if (value == ResType::WindowFocusStatus::WINDOW_UNFOCUS) {
        HandleUnfocusedWindow(windowId, windowType, displayId, pid, uid);
    }
}

void CgroupEventHandler::HandleFocusedWindow(uint32_t windowId, uint32_t windowType,
    uint64_t displayId, int32_t pid, int32_t uid)
{
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}" PRIu64 ", %{public}d, %{public}d",
        __func__, windowId, windowType, displayId, pid, uid);
    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;
    {
        ChronoScope cs("HandleFocusedWindow");
        app = supervisor_->GetAppRecordNonNull(uid);
        procRecord = app->GetProcessRecordNonNull(pid);
        auto win = procRecord->GetWindowInfoNonNull(windowId);
        procRecord->linkedWindow_ = win;
        win->windowType_ = (int32_t)(windowType);
        win->isFocused_ = true;
        win->displayId_ = displayId;

        app->focusedProcess_ = procRecord;
        supervisor_->focusedApp_ = app;
        CgroupAdjuster::GetInstance().AdjustAllProcessGroup(*(app.get()), AdjustSource::ADJS_FOCUSED_WINDOW);
        ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), ResType::RES_TYPE_WINDOW_FOCUS,
            ResType::WindowFocusStatus::WINDOW_FOCUS);
    }
    if (app->GetName().empty()) {
        app->SetName(SchedController::GetInstance().GetBundleNameByUid(uid));
    }
}

void CgroupEventHandler::HandleUnfocusedWindow(uint32_t windowId, uint32_t windowType,
    uint64_t displayId, int32_t pid, int32_t uid)
{
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}" PRIu64 ", %{public}d, %{public}d",
        __func__, windowId, windowType, displayId, pid, uid);
    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;
    {
        ChronoScope cs("HandleUnfocusedWindow");
        app = supervisor_->GetAppRecord(uid);
        procRecord = app ? app->GetProcessRecord(pid) : nullptr;
        if (!app || !procRecord) {
            return;
        }
        auto win = procRecord->GetWindowInfoNonNull(windowId);
        win->windowType_ = (int32_t)(windowType);
        win->isFocused_ = false;
        win->displayId_ = displayId;

        if (app->focusedProcess_ == procRecord) {
            app->focusedProcess_ = nullptr;
        }
        CgroupAdjuster::GetInstance().AdjustAllProcessGroup(*(app.get()), AdjustSource::ADJS_UNFOCUSED_WINDOW);
        ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), ResType::RES_TYPE_WINDOW_FOCUS,
            ResType::WindowFocusStatus::WINDOW_UNFOCUS);
    }
    if (app->GetName().empty()) {
        app->SetName(SchedController::GetInstance().GetBundleNameByUid(uid));
    }
}

void CgroupEventHandler::HandleWindowVisibilityChanged(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    int32_t windowId = 0;
    int32_t windowType = 0;
    int32_t pid = 0;
    int32_t uid = 0;

    if (!ParseValue(pid, "pid", payload) || !ParseValue(uid, "uid", payload) ||
        !ParseValue(windowId, "windowId", payload) || !ParseValue(windowType, "windowType", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }

    bool isVisible = (bool)value;
    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}d, %{public}d, %{public}d", __func__, windowId,
        isVisible, (int32_t)windowType, pid, uid);

    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;
    if (isVisible) {
        app = supervisor_->GetAppRecordNonNull(uid);
        procRecord = app->GetProcessRecordNonNull(pid);
    } else {
        app = supervisor_->GetAppRecord(uid);
        if (app) {
            procRecord = app->GetProcessRecord(pid);
        }
    }
    if (!procRecord) {
        return;
    }
    auto windowInfo = procRecord->GetWindowInfoNonNull(windowId);
    bool visibleStatusNotChanged = windowInfo->isVisible_ == isVisible;
    windowInfo->isVisible_ = isVisible;
    windowInfo->windowType_ = (int32_t)windowType;

    if (visibleStatusNotChanged) {
        return;
    }
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_WINDOW_VISIBILITY_CHANGED);
}

void CgroupEventHandler::HandleDrawingContentChangeWindow(uint32_t resType, int64_t value,
    const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    int32_t windowId = 0;
    int32_t windowType = 0;
    int32_t pid = 0;
    int32_t uid = 0;
    bool drawingContentState = (bool)value;

    if (!ParseValue(pid, "pid", payload) || !ParseValue(uid, "uid", payload) ||
        !ParseValue(windowId, "windowId", payload) || !ParseValue(windowType, "windowType", payload)) {
        CGS_LOGE("%{public}s: param error", __func__);
        return;
    }

    CGS_LOGD("%{public}s : %{public}d, %{public}d, %{public}d, %{public}d, %{public}d", __func__, windowId,
        drawingContentState, (int32_t)windowType, pid, uid);

    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }
    procRecord->processDrawingState_ = drawingContentState;
    auto windowInfo = procRecord->GetWindowInfoNonNull(windowId);
    if (!windowInfo) {
        CGS_LOGE("%{public}s : windowInfo nullptr!", __func__);
        return;
    }
    windowInfo->drawingContentState_ = drawingContentState;
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        ResType::RES_TYPE_WINDOW_DRAWING_CONTENT_CHANGE,
        drawingContentState ? ResType::WindowDrawingStatus::Drawing : ResType::WindowDrawingStatus::NotDrawing);
}

void CgroupEventHandler::HandleReportMMIProcess(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t mmi_service;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    if (!ParsePayload(uid, pid, mmi_service, value, payload)) {
        return;
    }

    CGS_LOGD("%{public}s : %{public}u, %{public}d, %{public}d, %{public}d",
        __func__, resType, uid, pid, mmi_service);
    if (uid <= 0 || pid <= 0 || mmi_service <= 0) {
        return;
    }

    auto app = supervisor_->GetAppRecordNonNull(uid);
    app->SetName(MMI_SERVICE_NAME);
    auto procRecord = app->GetProcessRecordNonNull(mmi_service);
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_MMI_SERVICE_THREAD);
}

void CgroupEventHandler::HandleReportRenderThread(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t render = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    if (!ParsePayload(uid, pid, render, value, payload)) {
        return;
    }

    CGS_LOGD("%{public}s : %{public}u, %{public}d, %{public}d, %{public}d",
        __func__, resType, uid, pid, render);
    if (uid <= 0 || pid <= 0 || render <= 0) {
        return;
    }

    auto app = supervisor_->GetAppRecordNonNull(uid);
    auto procRecord = app->GetProcessRecordNonNull(pid);
    procRecord->renderTid_ = render;
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_RENDER_THREAD);
}

void CgroupEventHandler::HandleReportKeyThread(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t keyTid = 0;
    int32_t role = 0;

    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;
    if (!GetProcInfoByPayload(uid, pid, app, procRecord, payload)) {
        return;
    }

    if (!ParseValue(keyTid, "tid", payload) || !ParseValue(role, "role", payload)) {
        return;
    }

    if (!ResSchedUtils::GetInstance().CheckTidIsInPid(pid, keyTid)) {
        return;
    }

    if (value == ResType::ReportChangeStatus::CREATE) {
        procRecord->keyThreadRoleMap_.emplace(keyTid, role);
    } else {
        procRecord->keyThreadRoleMap_.erase(keyTid);
    }

    CGS_LOGI("%{public}s : appName: %{public}s, uid: %{public}d, pid: %{public}d, keyTid: %{public}d",
        __func__, app->GetName().c_str(), uid, pid, keyTid);

    // if role of thread is important display, adjust it
    auto hostProcRecord = app->GetProcessRecord(procRecord->hostPid_);
    if (!hostProcRecord) {
        return;
    }
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(hostProcRecord.get()),
        AdjustSource::ADJS_REPORT_IMPORTANT_DISPLAY_THREAD);
}

void CgroupEventHandler::HandleReportWindowState(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t windowId = -1;
    int32_t state = 0;
    int32_t nowSerialNum = -1;

    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;
    if (!GetProcInfoByPayload(uid, pid, app, procRecord, payload)) {
        return;
    }

    if (!ParseValue(windowId, "windowId", payload) || !ParseValue(state, "state", payload) ||
        !ParseValue(nowSerialNum, "serialNum", payload)) {
        CGS_LOGW("%{public}s : param is not valid or not exist", __func__);
        return;
    }
    CGS_LOGI("%{public}s : render process name: %{public}s, uid: %{public}d, pid: %{public}d, state: %{public}d",
        __func__, app->GetName().c_str(), uid, pid, state);
    if (nowSerialNum <= procRecord->serialNum_ &&
        (procRecord->serialNum_ - nowSerialNum <= static_cast<int32_t>(MAX_SPAN_SERIAL))) {
        return;
    }
    procRecord->serialNum_ = nowSerialNum;

    UpdateActivepWebRenderInfo(state, windowId, procRecord, app);

    auto hostProcRecord = app->GetProcessRecord(procRecord->hostPid_);
    if (!hostProcRecord) {
        return;
    }

    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(hostProcRecord.get()),
        AdjustSource::ADJS_REPORT_WINDOW_STATE_CHANGED);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        ResType::RES_TYPE_REPORT_WINDOW_STATE, state);
}

void CgroupEventHandler::UpdateActivepWebRenderInfo(int32_t state, int32_t windowId,
    const std::shared_ptr<ProcessRecord>& proc, std::shared_ptr<Application>& app)
{
    if (supervisor_ == nullptr || app == nullptr) {
        CGS_LOGE("%{public}s : supervisor or app nullptr!", __func__);
        return;
    }

    if (state == ResType::WindowStates::ACTIVE) {
        std::shared_ptr<WindowInfo> win = nullptr;
        auto hostProc = app->GetProcessRecord(proc->hostPid_);
        if (hostProc != nullptr) {
            win = hostProc->GetWindowInfo(windowId);
        }
        if (win == nullptr) {
            if (!supervisor_->SearchWindowId(windowId, win)) {
                CGS_LOGE("%{public}s : windowId:%{public}d can't find. uid:%{public}d, pid:%{public}d",
                    __func__, windowId, proc->GetUid(), proc->GetPid());
                return;
            }
        }
        if (win != nullptr) {
            win->topWebviewRenderUid_ = proc->GetUid();
            proc->linkedWindow_ = win;
            proc->isActive_ = true;
        }
    } else {
        proc->linkedWindow_ = nullptr;
        proc->isActive_ = false;
    }

    CGS_LOGI("%{public}s : uid:%{public}d, pid:%{public}d, winId: %{public}d, isActive_: %{public}d",
        __func__, proc->GetUid(), proc->GetPid(), windowId, proc->isActive_);
}

void CgroupEventHandler::HandleReportAudioState(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        CGS_LOGE("%{public}s : payload does not contain uid or pid", __func__);
        return;
    }
    if (uid <= 0 || pid <= 0) {
        CGS_LOGE("%{public}s : uid or pid is less than 0", __func__);
        return;
    }

    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }

    procRecord->audioPlayingState_ = static_cast<int32_t>(value);
    CGS_LOGI("%{public}s :Appname:%{public}s, uid:%{public}d, pid:%{public}d, state:%{public}d",
        __func__, app->GetName().c_str(), uid, pid, procRecord->audioPlayingState_);

    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_AUDIO_STATE_CHANGED);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        resType, static_cast<int32_t>(value));
}

void CgroupEventHandler::HandleReportWebviewAudioState(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        return;
    }
    if (uid <= 0 || pid <= 0) {
        CGS_LOGW("%{public}s : uid or pid invalid, uid: %{public}d, pid: %{public}d!",
            __func__, uid, pid);
        return;
    }

    std::shared_ptr<Application> app = supervisor_->GetAppRecordNonNull(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        CGS_LOGW("%{public}s : proc record is not exist, uid: %{public}d, pid: %{public}d",
            __func__, uid, pid);
        return;
    }

    procRecord->audioPlayingState_ = static_cast<int32_t>(value);
    CGS_LOGI("%{public}s : audio process name: %{public}s, uid: %{public}d, pid: %{public}d, state: %{public}d",
        __func__, app->GetName().c_str(), uid, pid, procRecord->audioPlayingState_);

    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_WEBVIEW_AUDIO_STATE_CHANGED);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        resType, static_cast<int32_t>(value));
}

void CgroupEventHandler::HandleReportRunningLockEvent(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    uint32_t type = 0;
    int32_t state = -1;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr.", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        return;
    }
    if (uid <= 0 || pid <= 0) {
        return;
    }
    if (payload.contains("type") && payload.at("type").is_string()) {
        type = static_cast<uint32_t>(atoi(payload["type"].get<std::string>().c_str()));
    }
    state = static_cast<int32_t>(value);
    CGS_LOGI("report running lock event, uid:%{public}d, pid:%{public}d, lockType:%{public}d, state:%{public}d",
        uid, pid, type, state);
#ifdef POWER_MANAGER_ENABLE
    if (type == static_cast<uint32_t>(PowerMgr::RunningLockType::RUNNINGLOCK_PROXIMITY_SCREEN_CONTROL)) {
        return;
    }
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }
    procRecord->runningLockState_[type] = (state == ResType::RunninglockState::RUNNINGLOCK_STATE_ENABLE);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), resType, state);
#endif
}

void CgroupEventHandler::HandleReportBluetoothConnectState(
    uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr.", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        CGS_LOGE("%{public}s : payload does not contain uid or pid", __func__);
        return;
    }
    if (uid <= 0 || pid <= 0) {
        CGS_LOGE("%{public}s : uid or pid is less than 0", __func__);
        return;
    }
    CGS_LOGD("report bluetooth connect state, uid:%{public}d, pid:%{public}d, value:%{public}lld",
        uid, pid, (long long)value);
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }
    procRecord->bluetoothState_ = static_cast<int32_t>(value);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        resType, static_cast<int32_t>(value));
}

void CgroupEventHandler::HandleReportHisysEvent(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr.", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        return;
    }
    if (uid <= 0 || pid <= 0) {
        return;
    }
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }

    switch (resType) {
        case ResType::RES_TYPE_REPORT_CAMERA_STATE: {
            procRecord->cameraState_ = static_cast<int32_t>(value);
            break;
        }
        case ResType::RES_TYPE_WIFI_CONNECT_STATE_CHANGE: {
            procRecord->wifiState_ = static_cast<int32_t>(value);
            break;
        }
        case ResType::RES_TYPE_MMI_INPUT_STATE: {
            if (payload.contains("syncStatus") && payload.at("syncStatus").is_string()) {
                procRecord->mmiStatus_ = atoi(payload["syncStatus"].get<std::string>().c_str());
            }
            break;
        }
        default: {
            break;
        }
    }
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        resType, static_cast<int32_t>(value));
}

void CgroupEventHandler::HandleReportScreenCaptureEvent(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr.", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        CGS_LOGE("%{public}s : payload does not contain uid or pid", __func__);
        return;
    }
    if (uid <= 0 || pid <= 0) {
        CGS_LOGE("%{public}s : uid or pid is less than 0", __func__);
        return;
    }
    CGS_LOGD("report Screen capture, uid:%{public}d, pid:%{public}d, value:%{public}lld",
        uid, pid, (long long)value);
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }

    procRecord->screenCaptureState_ = (value == ResType::ScreenCaptureStatus::START_SCREEN_CAPTURE);
    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_SCREEN_CAPTURE);

    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()),
        resType, static_cast<int32_t>(value));
}

void CgroupEventHandler::HandleReportAvCodecEvent(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;
    int32_t instanceId = -1;
    int32_t state = -1;

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr.", __func__);
        return;
    }

    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        return;
    }
    if (uid <= 0 || pid <= 0) {
        return;
    }
    if (!ParseValue(instanceId, "instanceId", payload)) {
        return;
    }
    if (instanceId < 0) {
        return;
    }
    state = static_cast<int32_t>(value);
    CGS_LOGI("report av_codec event, uid:%{public}d, pid:%{public}d, instanceId:%{public}d, state:%{public}d",
        uid, pid, instanceId, state);
    std::shared_ptr<Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr<ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        return;
    }
    procRecord->avCodecState_[instanceId] = (state == ResType::AvCodecState::CODEC_START_INFO);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), resType, state);
}

void CgroupEventHandler::HandleSceneBoardState(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t sceneBoardPid = 0;
    int32_t sceneBoardUid = 0;
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }

    if (!ParseValue(sceneBoardPid, "pid", payload) || !ParseValue(sceneBoardUid, "uid", payload)) {
        return;
    }
    if (sceneBoardPid <= 0) {
        return;
    }
    supervisor_->sceneBoardUid_ = sceneBoardUid;
    supervisor_->sceneBoardPid_ = sceneBoardPid;
    CGS_LOGI("%{public}s:pid[%{public}d],uid[%{public}d]", __func__, sceneBoardPid, sceneBoardUid);
}

void CgroupEventHandler::HandleWebviewScreenCapture(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;

    if (!GetProcInfoByPayload(uid, pid, app, procRecord, payload)) {
        return;
    }

    procRecord->screenCaptureState_= (value == ResType::WebScreenCapture::WEB_SCREEN_CAPTURE_START);
    CGS_LOGI("%{public}s : screen capture process: %{public}s, uid: %{public}d, pid: %{public}d, state: %{public}d",
        __func__, app->GetName().c_str(), uid, pid, procRecord->screenCaptureState_);

    CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
        AdjustSource::ADJS_REPORT_WEBVIEW_SCREEN_CAPTURE);
    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), resType,
        procRecord->screenCaptureState_);
}

void CgroupEventHandler::ReportAbilityStatus(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t saId = -1;
    if (payload.contains("saId") && payload.at("saId").is_number_integer()) {
        saId = payload["saId"].get<int32_t>();
    }
    std::string deviceId = "";
    if (payload.contains("deviceId") && payload.at("deviceId").is_string()) {
        deviceId = payload["deviceId"].get<std::string>();
    }
    CGS_LOGD("%{public}s saId: %{public}d, status: %{public}lld", __func__, saId, (long long)value);
    PostTask([saId, deviceId, value, this] {
        if (value > 0) {
            HandleAbilityAdded(saId, deviceId);
        } else {
            HandleAbilityRemoved(saId, deviceId);
        }
    });
}

void CgroupEventHandler::UpdateMmiStatus(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (supervisor_ == nullptr) {
        return;
    }
    if (!payload.contains("pid") || !payload.at("pid").is_number_integer()) {
        return;
    }
    if (!payload.contains("uid") || !payload.at("uid").is_number_integer()) {
        return;
    }
    if (!payload.contains("status") || !payload.at("status").is_number_integer()) {
        return;
    }
    int32_t pid = payload["pid"].get<int32_t>();
    int32_t uid = payload["uid"].get<int32_t>();
    int32_t status = payload["status"].get<int32_t>();
    auto app = supervisor_->GetAppRecord(uid);
    auto procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (procRecord) {
        procRecord->mmiStatus_ = status;
    }
}

void CgroupEventHandler::HandleEmptyPayloadForCosmicCubeState(uint32_t resType, int64_t value)
{
    bool isNeedRecover = resType == ResType::RES_TYPE_COSMIC_CUBE_STATE_CHANGE &&
        value == ResType::CosmicCubeState::APPLICATION_ABOUT_TO_RECOVER;
    if (!isNeedRecover) {
        return;
    }
    std::map <int32_t, std::shared_ptr<Application>> uidMap = supervisor_->GetUidsMap();
    for (auto it = uidMap.begin(); it != uidMap.end(); it++) {
        int32_t uid = it->first;
        std::shared_ptr <Application> app = it->second;
        if (!app->isCosmicCubeStateHide_) {
            continue;
        }
        app->isCosmicCubeStateHide_ = false;
        std::map <pid_t, std::shared_ptr<ProcessRecord>> pidMap = app->GetPidsMap();
        for (auto pidIt = pidMap.begin(); pidIt != pidMap.end(); pidIt++) {
            int32_t pid = pidIt->first;
            std::shared_ptr <ProcessRecord> procRecord = pidIt->second;
            if (procRecord->processType_ == ProcRecordType::NORMAL) {
                CGS_LOGI("%{public}s, uid:%{public}d pid:%{public}d recover", __func__, uid, pid);
                CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
                    AdjustSource::ADJS_PROCESS_STATE);
            }
        }
    }
}

void CgroupEventHandler::HandleReportCosmicCubeState(uint32_t resType, int64_t value, const nlohmann::json &payload)
{
    if (supervisor_ == nullptr) {
        return;
    }
    int32_t uid = 0;
    int32_t pid = 0;
    if (!ParsePayload(uid, pid, payload)) {
        CGS_LOGW("%{public}s : uid or pid invalid, uid:%{public}d, pid:%{public}d, value:%{public}lld",
            __func__, uid, pid, (long long)value);
        HandleEmptyPayloadForCosmicCubeState(resType, value);
        return;
    }
    std::shared_ptr <Application> app = supervisor_->GetAppRecord(uid);
    std::shared_ptr <ProcessRecord> procRecord = app ? app->GetProcessRecord(pid) : nullptr;
    if (!app || !procRecord) {
        CGS_LOGW("%{public}s : app or proc record is not exist, uid:%{public}d, pid:%{public}d!", __func__, uid, pid);
        return;
    }
    app->isCosmicCubeStateHide_ = (value == ResType::CosmicCubeState::APPLICATION_ABOUT_TO_HIDE);
    if (procRecord->processType_ == ProcRecordType::NORMAL) {
        CGS_LOGI("%{public}s uid:%{public}d, pid:%{public}d, value:%{public}lld", __func__, uid, pid, (long long)value);
        CgroupAdjuster::GetInstance().AdjustProcessGroup(*(app.get()), *(procRecord.get()),
            AdjustSource::ADJS_PROCESS_STATE);
    }
}

void CgroupEventHandler::HandleReportWebviewVideoState(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    int32_t uid = 0;
    int32_t pid = 0;

    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<ProcessRecord> procRecord = nullptr;

    if (!GetProcInfoByPayload(uid, pid, app, procRecord, payload)) {
        return;
    }

    procRecord->videoState_ = (value == ResType::WebVideoState::WEB_VIDEO_PLAYING_START);
    CGS_LOGI("%{public}s : video process name: %{public}s, uid: %{public}d, pid: %{public}d, state: %{public}d",
        __func__, app->GetName().c_str(), uid, pid, procRecord->videoState_);

    ResSchedUtils::GetInstance().ReportSysEvent(*(app.get()), *(procRecord.get()), resType,
        procRecord->videoState_);
}

void CgroupEventHandler::HandleOnAppStopped(uint32_t resType, int64_t value, const nlohmann::json& payload)
{
    if (!payload.contains("uid") || !payload.at("uid").is_number_integer()) {
        CGS_LOGE("%{public}s : uid invalid!", __func__);
        return;
    }
    int32_t uid = payload["uid"].get<int32_t>();
    if (!payload.contains("bundleName") || !payload.at("bundleName").is_string()) {
        CGS_LOGE("%{public}s : bundleName invalid!", __func__);
        return;
    }
    std::string bundleName = payload["bundleName"].get<std::string>();

    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return;
    }
    CGS_LOGI("%{public}s : %{public}d, %{public}s", __func__, uid, bundleName.c_str());
    supervisor_->RemoveApplication(uid);
}

bool CgroupEventHandler::GetProcInfoByPayload(int32_t &uid, int32_t &pid, std::shared_ptr<Application>& app,
    std::shared_ptr<ProcessRecord>& procRecord, const nlohmann::json& payload)
{
    if (!supervisor_) {
        CGS_LOGE("%{public}s : supervisor nullptr!", __func__);
        return false;
    }

    if (!ParsePayload(uid, pid, payload)) {
        CGS_LOGW("%{public}s : uid or pid invalid, uid: %{public}d, pid: %{public}d!",
            __func__, uid, pid);
        return false;
    }
    app = supervisor_->GetAppRecord(uid);
    if (app) {
        procRecord = app->GetProcessRecordNonNull(pid);
    }
    if (!app || !procRecord) {
        CGS_LOGW("%{public}s : app record or proc record is not exist, uid: %{public}d, pid: %{public}d!",
            __func__, uid, pid);
        return false;
    }
    return true;
}

bool CgroupEventHandler::ParsePayload(int32_t& uid, int32_t& pid, const nlohmann::json& payload)
{
    if (!ParseValue(uid, "uid", payload) || !ParseValue(pid, "pid", payload)) {
        return false;
    }
    if (uid <= 0 || pid <= 0) {
        return false;
    }
    return true;
}

bool CgroupEventHandler::ParsePayload(int32_t& uid, int32_t& pid, int32_t& tid,
    int64_t value, const nlohmann::json& payload)
{
    if (payload.contains("uid") && payload.at("uid").is_string()) {
        uid = atoi(payload["uid"].get<std::string>().c_str());
    }

    if (payload.contains("pid") && payload.at("pid").is_string()) {
        pid = atoi(payload["pid"].get<std::string>().c_str());
    }
    tid = static_cast<int32_t>(value);
    return true;
}

bool CgroupEventHandler::ParseString(std::string& value, const char* name,
    const nlohmann::json& payload)
{
    if (payload.contains(name) && payload.at(name).is_string()) {
        value = payload[name].get<std::string>();
        return true;
    }
    return false;
}

bool CgroupEventHandler::ParseValue(int32_t& value, const char* name,
    const nlohmann::json& payload)
{
    if (payload.contains(name) && payload.at(name).is_string()) {
        value = atoi(payload[name].get<std::string>().c_str());
        return true;
    }
    return false;
}

bool CgroupEventHandler::ParseLongValue(int64_t& value, const char* name,
    const nlohmann::json& payload)
{
    if (payload.contains(name) && payload.at(name).is_string()) {
        value = atoll(payload[name].get<std::string>().c_str());
        return true;
    }
    return false;
}

void CgroupEventHandler::PostTask(const std::function<void()> task)
{
    if (!cgroupEventQueue_) {
        CGS_LOGE("%{public}s : cgroupEventQueue_ nullptr", __func__);
        return;
    }
    cgroupEventQueue_->submit([task, this] {
        task();
    });
}

void CgroupEventHandler::PostTask(const std::function<void()> task, const std::string &taskName,
    const int32_t delayTime)
{
    std::lock_guard<ffrt::mutex> autoLock(delayTaskMapMutex_);
    if (!cgroupEventQueue_) {
        CGS_LOGE("%{public}s : cgroupEventQueue_ nullptr", __func__);
        return;
    }
    delayTaskMap_[taskName] = cgroupEventQueue_->submit_h([task, this] {
        task();
    }, ffrt::task_attr().delay(delayTime * ffrtSwitch_));
}

void CgroupEventHandler::PostTaskAndWait(const std::function<void()> task)
{
    if (!cgroupEventQueue_) {
        CGS_LOGE("%{public}s : cgroupEventQueue_ nullptr", __func__);
        return;
    }
    ffrt::task_handle handle = cgroupEventQueue_->submit_h(task);
    cgroupEventQueue_->wait(handle);
}

void CgroupEventHandler::RemoveTask(const std::string &taskName)
{
    std::lock_guard<ffrt::mutex> autoLock(delayTaskMapMutex_);
    for (auto iter = delayTaskMap_.begin(); iter != delayTaskMap_.end(); iter++) {
        if (iter->first == taskName && iter->second != nullptr) {
            cgroupEventQueue_->cancel(iter->second);
            delayTaskMap_.erase(iter);
            return;
        }
    }
}
} // namespace ResourceSchedule
} // namespace OHOS
