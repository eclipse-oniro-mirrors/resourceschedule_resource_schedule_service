/*
 * Copyright (c) 2022-2025 Huawei Device Co., Ltd.
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

#include "event_controller.h"

#include "application_info.h"
#include "bundle_constants.h"
#include "bundle_mgr_interface.h"
#include "common_event_manager.h"
#include "common_event_support.h"
#include "hisysevent.h"
#include "if_system_ability_manager.h"
#include "ipc_skeleton.h"
#include "iservice_registry.h"
#include "oobe_datashare_utils.h"
#include "oobe_manager.h"
#include "system_ability_definition.h"

#include "res_sched_log.h"
#include "res_sched_mgr.h"
#include "res_type.h"

using namespace OHOS::EventFwk;
namespace OHOS {
namespace ResourceSchedule {
IMPLEMENT_SINGLE_INSTANCE(EventController);

static const char* COMMON_EVENT_CAMERA_STATUS = "usual.event.CAMERA_STATUS";
static const char* COMMON_EVENT_GAME_STATUS = "usual.event.gameservice.GAME_STATUS_CHANGE_UNI";
static const char* DATA_SHARE_READY = "usual.event.DATA_SHARE_READY";
static const char* CONFIG_UPDATE_ACTION = "usual.event.DUA_SA_CFG_UPDATED";
static const char* DEVICE_MODE_PAYMODE_NAME = "deviceMode";
static const char* DEVICE_MODE_TYPE_KEY = "deviceModeType";
static const char* SCENE_BOARD_NAME = "com.ohos.sceneboard";
static const char* CAMERA_STATE = "cameraState";
static const char* CAMERA_TYPE = "cameraType";
static const char* IS_SYSTEM_CAMERA = "isSystemCamera";
static const char* EVENT_INFO_TYPE = "type";
static const char* EVENT_INFO_SUBTYPE = "subtype";
static const char* CONFIG_TYPE = "RSSCONFIG";
static const char* GAME_UID = "uid";
static const char* GAME_STATUS = "type";
static const char* GAME_ENV = "env";
static const char* COMMON_EVENT_CAPACITY = "soc";
static const char* COMMON_EVENT_CHARGE_STATE = "chargeState";
static const char* COMMON_EVENT_USER_SLEEP_STATE_CHANGED = "common.event.USER_NOT_CARE_CHARGE_SLEEP";

void EventController::Init()
{
    if (sysAbilityListener_ != nullptr) {
        return;
    }
    sysAbilityListener_ = new (std::nothrow) SystemAbilityStatusChangeListener();
    if (sysAbilityListener_ == nullptr) {
        RESSCHED_LOGE("Failed to create statusChangeListener due to no memory.");
        HiSysEventWrite(HiviewDFX::HiSysEvent::Domain::RSS, "INIT_FAULT", HiviewDFX::HiSysEvent::EventType::FAULT,
                        "COMPONENT_NAME", "MAIN",
                        "ERR_TYPE", "others",
                        "ERR_MSG", "EventController new statusChangeListener object failed!");
        return;
    }
    sptr<ISystemAbilityManager> systemAbilityManager
        = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (systemAbilityManager == nullptr) {
        RESSCHED_LOGE("systemAbilityManager is null");
        HiSysEventWrite(HiviewDFX::HiSysEvent::Domain::RSS, "INIT_FAULT", HiviewDFX::HiSysEvent::EventType::FAULT,
                        "COMPONENT_NAME", "MAIN",
                        "ERR_TYPE", "register failure",
                        "ERR_MSG", "EventController get system ability manager failed!");
        sysAbilityListener_ = nullptr;
        return;
    }
    int32_t ret = systemAbilityManager->SubscribeSystemAbility(COMMON_EVENT_SERVICE_ID, sysAbilityListener_);
    if (ret != ERR_OK) {
        RESSCHED_LOGE("subscribe system ability id: %{public}d failed", COMMON_EVENT_SERVICE_ID);
        HiSysEventWrite(HiviewDFX::HiSysEvent::Domain::RSS, "INIT_FAULT", HiviewDFX::HiSysEvent::EventType::FAULT,
                        "COMPONENT_NAME", "MAIN",
                        "ERR_TYPE", "register failure",
                        "ERR_MSG", "EventController subscribe the event service SA failed!");
        sysAbilityListener_ = nullptr;
    } else {
        RESSCHED_LOGI("subscribe system ability id: %{public}d succeed", COMMON_EVENT_SERVICE_ID);
    }
}

void EventController::HandlePkgAddRemove(const EventFwk::Want &want, nlohmann::json &payload) const
{
    AppExecFwk::ElementName ele = want.GetElement();
    std::string bundleName = ele.GetBundleName();
    int32_t uid = want.GetIntParam(AppExecFwk::Constants::UID, -1);
    int32_t appIndex = want.GetIntParam("appIndex", -1);
    payload["bundleName"] = bundleName;
    payload["uid"] = uid;
    payload["appIndex"] = appIndex;
}

int32_t EventController::GetUid(const int32_t &userId, const std::string &bundleName) const
{
    AppExecFwk::ApplicationInfo info;
    sptr<ISystemAbilityManager> systemAbilityManager
        = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (systemAbilityManager == nullptr) {
        RESSCHED_LOGE("Failed to get uid due to get systemAbilityManager is null.");
        return -1;
    }
    sptr<IRemoteObject> remoteObject  = systemAbilityManager->GetSystemAbility(BUNDLE_MGR_SERVICE_SYS_ABILITY_ID);
    if (remoteObject == nullptr) {
        RESSCHED_LOGE("Failed to get uid due to get BMS is null.");
        return -1;
    }
    sptr<AppExecFwk::IBundleMgr> bundleMgr = iface_cast<AppExecFwk::IBundleMgr>(remoteObject);
    if (bundleMgr == nullptr) {
        RESSCHED_LOGE("Failed to get uid due to get bundleMgr is null.");
        return -1;
    }
    bundleMgr->GetApplicationInfo(bundleName, AppExecFwk::ApplicationFlag::GET_BASIC_APPLICATION_INFO, userId, info);
    return static_cast<int32_t>(info.uid);
}

void EventController::HandleConnectivityChange(
    const EventFwk::Want &want, const int32_t &code, nlohmann::json &payload)
{
    int32_t netType = want.GetIntParam("NetType", -1);
    if (netType != 1) {
        return;
    }
    ReportDataInProcess(ResType::RES_TYPE_WIFI_CONNECT_STATE_CHANGE, code, payload);
}

void EventController::ReportDataInProcess(const uint32_t &resType, const int64_t &value, const nlohmann::json& payload)
{
    ResSchedMgr::GetInstance().ReportData(resType, value, payload);
    resType_ = resType;
    value_ = value;
    payload_ = payload;
}

void EventController::Stop()
{
    if (sysAbilityListener_ == nullptr) {
        return;
    }
    sysAbilityListener_->Stop();
}

void EventController::DataShareIsReady()
{
    RESSCHED_LOGI("Data_share is ready! Call back to create data_share helper");
    ResourceSchedule::OOBEManager::GetInstance().OnReceiveDataShareReadyCallBack();
}

inline void SubscribeCommonEvent(std::shared_ptr<EventController> subscriber)
{
    if (CommonEventManager::SubscribeCommonEvent(subscriber)) {
        RESSCHED_LOGI("SubscribeCommonEvent ok");
    } else {
        RESSCHED_LOGW("SubscribeCommonEvent fail");
        HiSysEventWrite(HiviewDFX::HiSysEvent::Domain::RSS, "INIT_FAULT", HiviewDFX::HiSysEvent::EventType::FAULT,
                        "COMPONENT_NAME", "MAIN",
                        "ERR_TYPE", "register failure",
                        "ERR_MSG", "EventController subscribe common events failed!");
    }
}

void EventController::SystemAbilityStatusChangeListener::OnAddSystemAbility(
    int32_t systemAbilityId, const std::string& deviceId)
{
    RESSCHED_LOGI("common event service is added");
    MatchingSkills matchingSkills;
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_CONNECTIVITY_CHANGE);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_INSTALLATION_STARTED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_ADDED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_REPLACED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_SCREEN_ON);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_SCREEN_OFF);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_SWITCHED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_TIMEZONE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_BUNDLE_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_FULLY_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_TIME_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_NITZ_TIME_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_NITZ_TIMEZONE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_CHARGING);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USB_DEVICE_ATTACHED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_DISCHARGING);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USB_DEVICE_DETACHED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_CALL_STATE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_WIFI_P2P_STATE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_POWER_SAVE_MODE_CHANGED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_BOOT_COMPLETED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_POWER_CONNECTED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_POWER_DISCONNECTED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_WIFI_POWER_STATE);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_BATTERY_CHANGED);
    matchingSkills.AddEvent(DATA_SHARE_READY);
    matchingSkills.AddEvent(COMMON_EVENT_CAMERA_STATUS);
    matchingSkills.AddEvent(CONFIG_UPDATE_ACTION);
    matchingSkills.AddEvent(COMMON_EVENT_GAME_STATUS);
    matchingSkills.AddEvent(COMMON_EVENT_USER_SLEEP_STATE_CHANGED);
    CommonEventSubscribeInfo subscriberInfo(matchingSkills);
    subscriber_ = std::make_shared<EventController>(subscriberInfo);
    SubscribeCommonEvent(subscriber_);
    SubscribeLockScreenCommonEvent();
}

void EventController::SystemAbilityStatusChangeListener::SubscribeLockScreenCommonEvent()
{
    MatchingSkills lockScreenSkills;
    lockScreenSkills.AddEvent("common.event.UNLOCK_SCREEN");
    lockScreenSkills.AddEvent("common.event.LOCK_SCREEN");
    CommonEventSubscribeInfo subscriberInfo(lockScreenSkills);
    subscriberInfo.SetPublisherBundleName(SCENE_BOARD_NAME);
    lockScreenSubscriber_ = std::make_shared<EventController>(subscriberInfo);
    SubscribeCommonEvent(lockScreenSubscriber_);
}

void EventController::OnReceiveEvent(const EventFwk::CommonEventData &data)
{
    Want want = data.GetWant();
    std::string action = want.GetAction();
    RESSCHED_LOGD("Recieved common event:%{public}s", action.c_str());

    nlohmann::json payload = nlohmann::json::object();
    if (HandlePkgCommonEvent(action, want, payload)) {
        return;
    }
    if (action == CommonEventSupport::COMMON_EVENT_SCREEN_ON) {
        ReportDataInProcess(ResType::RES_TYPE_SCREEN_STATUS, ResType::ScreenStatus::SCREEN_ON, payload);
        return;
    }
    if (action == CommonEventSupport::COMMON_EVENT_SCREEN_OFF) {
        ReportDataInProcess(ResType::RES_TYPE_SCREEN_STATUS, ResType::ScreenStatus::SCREEN_OFF, payload);
        return;
    }
    if (action == CommonEventSupport::COMMON_EVENT_CONNECTIVITY_CHANGE) {
        int32_t code = data.GetCode();
        HandleConnectivityChange(want, code, payload);
        return;
    }
    if (action == CommonEventSupport::COMMON_EVENT_USER_SWITCHED) {
        int32_t userId = data.GetCode();
        ReportDataInProcess(ResType::RES_TYPE_USER_SWITCH, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == CommonEventSupport::COMMON_EVENT_USER_REMOVED) {
        int32_t userId = data.GetCode();
        ReportDataInProcess(ResType::RES_TYPE_USER_REMOVE, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == "common.event.UNLOCK_SCREEN") {
        ReportDataInProcess(ResType::RES_TYPE_SCREEN_LOCK, ResType::ScreenLockStatus::SCREEN_UNLOCK, payload);
        return;
    }
    if (action == "common.event.LOCK_SCREEN") {
        ReportDataInProcess(ResType::RES_TYPE_SCREEN_LOCK, ResType::ScreenLockStatus::SCREEN_LOCK, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_CALL_STATE_CHANGED) {
        payload["state"] = want.GetIntParam("state", -1);
        ReportDataInProcess(ResType::RES_TYPE_CALL_STATE_CHANGED, static_cast<int64_t>(data.GetCode()), payload);
        return;
    }
    if (action == DATA_SHARE_READY) {
        DataShareIsReady();
        return;
    }

    handleEvent(data.GetCode(), action, payload, want);
}

void EventController::handleEvent(int32_t userId, const std::string &action, nlohmann::json &payload, Want &want)
{
    if (action == CommonEventSupport::COMMON_EVENT_TIMEZONE_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_TIMEZONE_CHANGED, ResType::RES_TYPE_TIMEZONE_CHANGED, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_TIME_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_TIME_CHANGED, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_NITZ_TIME_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_NITZ_TIME_CHANGED, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_NITZ_TIMEZONE_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_NITZ_TIMEZONE_CHANGED, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_CHARGING) {
        ReportDataInProcess(ResType::RES_TYPE_CHARGING_DISCHARGING, ResType::ChargeStatus::EVENT_CHARGING, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_DISCHARGING) {
        ReportDataInProcess(ResType::RES_TYPE_CHARGING_DISCHARGING, ResType::ChargeStatus::EVENT_DISCHARGING, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_ATTACHED) {
        ReportDataInProcess(ResType::RES_TYPE_USB_DEVICE, ResType::UsbDeviceStatus::USB_DEVICE_ATTACHED, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_DETACHED) {
        ReportDataInProcess(ResType::RES_TYPE_USB_DEVICE, ResType::UsbDeviceStatus::USB_DEVICE_DETACHED, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_WIFI_P2P_STATE_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_WIFI_P2P_STATE_CHANGED, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_POWER_SAVE_MODE_CHANGED) {
        ReportDataInProcess(ResType::RES_TYPE_POWER_MODE_CHANGED, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_WIFI_POWER_STATE) {
        payload["state"] = std::to_string(userId);
        ReportDataInProcess(ResType::RES_TYPE_WIFI_POWER_STATE_CHANGE, static_cast<int64_t>(userId), payload);
        return;
    }
    handleOtherEvent(userId, action, payload, want);
}

void EventController::handleOtherEvent(int32_t userId, const std::string &action, nlohmann::json &payload, Want &want)
{
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_BOOT_COMPLETED) {
        RESSCHED_LOGI("report boot completed");
        ReportDataInProcess(ResType::RES_TYPE_BOOT_COMPLETED,
            ResType::BootCompletedStatus::START_BOOT_COMPLETED, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_POWER_CONNECTED) {
        payload[DEVICE_MODE_TYPE_KEY] = "powerConnectStatus";
        payload[DEVICE_MODE_PAYMODE_NAME] = "powerConnected";
        ReportDataInProcess(ResType::RES_TYPE_DEVICE_MODE_STATUS,
            ResType::DeviceModeStatus::MODE_ENTER, payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_POWER_DISCONNECTED) {
        payload[DEVICE_MODE_TYPE_KEY] = "powerConnectStatus";
        payload[DEVICE_MODE_PAYMODE_NAME] = "powerDisConnected";
        ReportDataInProcess(ResType::RES_TYPE_DEVICE_MODE_STATUS,
            ResType::DeviceModeStatus::MODE_ENTER, payload);
        return;
    }
    if (action == COMMON_EVENT_CAMERA_STATUS) {
        RESSCHED_LOGI("report camera status completed");
        payload[CAMERA_TYPE] = want.GetIntParam(IS_SYSTEM_CAMERA, 0);
        ReportDataInProcess(ResType::RES_TYPE_REPORT_CAMERA_STATE,
            static_cast<int64_t>(want.GetIntParam(CAMERA_STATE, 1)), payload);
        return;
    }
    if (action == CONFIG_UPDATE_ACTION) {
        RESSCHED_LOGI("report param update event");
        payload["type"] = want.GetStringParam(EVENT_INFO_TYPE);
        payload["subtype"] = want.GetStringParam(EVENT_INFO_SUBTYPE);
        ReportDataInProcess(ResType::RES_TYPE_PARAM_UPADTE_EVENT, static_cast<int64_t>(userId), payload);
        return;
    }
    if (action == COMMON_EVENT_GAME_STATUS) {
        RESSCHED_LOGI("report game status event");
        payload[GAME_UID] = want.GetIntParam(GAME_UID, -1);
        payload[GAME_ENV] = want.GetIntParam(GAME_ENV, -1);
        ReportDataInProcess(ResType::RES_TYPE_REPORT_GAME_STATE_CHANGE,
            static_cast<int64_t>(want.GetIntParam(GAME_STATUS, -1)), payload);
        return;
    }
    handleLeftEvent(userId, action, payload, want);
}

void EventController::handleLeftEvent(int32_t userId, const std::string &action, nlohmann::json &payload, Want &want)
{
    if (action == COMMON_EVENT_USER_SLEEP_STATE_CHANGED) {
        RESSCHED_LOGI("report sleep state event");
        ReportDataInProcess(ResType::RES_TYPE_USER_NOT_CARE_CHARGE_SLEEP,
            static_cast<int64_t>(want.GetBoolParam("isSleep", false)), payload);
        return;
    }
    if (action == EventFwk::CommonEventSupport::COMMON_EVENT_BATTERY_CHANGED) {
        RESSCHED_LOGI("report battery status change event");
        payload[COMMON_EVENT_CHARGE_STATE] = want.GetIntParam(COMMON_EVENT_CHARGE_STATE, -1);
        ReportDataInProcess(ResType::RES_TYPE_REPORT_BATTERY_STATUS_CHANGE,
            static_cast<int64_t>(want.GetIntParam(COMMON_EVENT_CAPACITY, -1)), payload);
    }
}

bool EventController::HandlePkgCommonEvent(const std::string &action, Want &want, nlohmann::json &payload)
{
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_REMOVED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL, ResType::AppInstallStatus::APP_UNINSTALL, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_INSTALLATION_STARTED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL,
            ResType::AppInstallStatus::APP_INSTALL_START, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_ADDED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL,
            ResType::AppInstallStatus::APP_INSTALL_END, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_CHANGED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL, ResType::AppInstallStatus::APP_CHANGED, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_REPLACED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL, ResType::AppInstallStatus::APP_REPLACED, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_FULLY_REMOVED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL,
                            ResType::AppInstallStatus::APP_FULLY_REMOVED, payload);
        return true;
    }
    if (action == CommonEventSupport::COMMON_EVENT_BUNDLE_REMOVED) {
        HandlePkgAddRemove(want, payload);
        ReportDataInProcess(ResType::RES_TYPE_APP_INSTALL_UNINSTALL,
                            ResType::AppInstallStatus::BUNDLE_REMOVED, payload);
        return true;
    }
    return false;
}

void EventController::SystemAbilityStatusChangeListener::OnRemoveSystemAbility(
    int32_t systemAbilityId, const std::string& deviceId)
{
    RESSCHED_LOGW("common event service is removed.");
    subscriber_ = nullptr;
    lockScreenSubscriber_ = nullptr;
}

void EventController::SystemAbilityStatusChangeListener::Stop()
{
    if (subscriber_ == nullptr || lockScreenSubscriber_ == nullptr) {
        return;
    }
    CommonEventManager::UnSubscribeCommonEvent(subscriber_);
    CommonEventManager::UnSubscribeCommonEvent(lockScreenSubscriber_);
    RESSCHED_LOGI("unsubscribe all common event.");
    subscriber_ = nullptr;
    lockScreenSubscriber_ = nullptr;
}

extern "C" void EventControllerInit()
{
    EventController::GetInstance().Init();
}

extern "C" void EventControllerStop()
{
    EventController::GetInstance().Stop();
}
}
}