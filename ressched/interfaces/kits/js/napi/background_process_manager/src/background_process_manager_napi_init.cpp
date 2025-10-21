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

#include "background_process_manager_napi_init.h"

#include <map>
#include <string>

#include "background_process_manager.h"
#include "res_sched_log.h"

namespace {
    constexpr int SET_PROCESS_PRIORITY_PARAM_NUM = 2;
    constexpr int RESET_PROCESS_PRIORITY_PARAM_NUM = 1;
    constexpr int SET_POWER_SAVE_MODE_PARAM_NUM = 2;
    constexpr int IS_POWER_SAVE_MODE_PARAM_NUM = 1;
    constexpr int PID_INDEX = 0;
    constexpr int PRIORITY_INDEX = 1;
    constexpr int MODE_INDEX = 1;
    constexpr int IS_POWER_SAVE_OK = 1;
    constexpr int IS_POWER_SAVE_NOK = 0;
    std::map<int, std::string> errCodeMsg = {
        { ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM,
            "Parameter error. Possible causes: priority is out of range." },
        { ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR,
            "Parameter error. Possible causes: priority is out of range." },
        { ERR_BACKGROUND_PROCESS_MANAGER_PERMISSION_DENIED,
            "Permission denied." },
        { ERR_BACKGROUND_PROCESS_MANAGER_CAPABILITY_NOT_SUPPORTED,
            "Capability not supported." },
        { ERR_BACKGROUND_PROCESS_MANAGER_SETUP_ERROR,
            "This setting is overridden by setting in Task Manager." },
        { ERR_BACKGROUND_PROCESS_MANAGER_SYSTEM_SCHEDULING,
            "The setting failed due to system scheduling reasons." }
    };
}

namespace OHOS {
namespace ResourceSchedule {
#ifdef __cplusplus
extern "C" {
#endif

static void HandleErrorCode(napi_env env, int errorCode)
{
    if (errCodeMsg.find(errorCode) == errCodeMsg.end()) {
        return;
    }
    std::string msg;
    msg.append(errCodeMsg[errorCode]);
    napi_throw_error(env, std::to_string(errorCode).c_str(), msg.c_str());
}

napi_value SetProcessPriority(napi_env env, napi_callback_info info)
{
    napi_value ret;
    size_t argc = SET_PROCESS_PRIORITY_PARAM_NUM;
    napi_value argv[SET_PROCESS_PRIORITY_PARAM_NUM] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc != SET_PROCESS_PRIORITY_PARAM_NUM) {
        RESSCHED_LOGE("param num error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM);
        return ret;
    }

    napi_valuetype pidType;
    napi_typeof(env, argv[PID_INDEX], &pidType);

    napi_valuetype priorityType;
    napi_typeof(env, argv[PRIORITY_INDEX], &priorityType);

    if (pidType != napi_number || priorityType != napi_number) {
        RESSCHED_LOGE("param type error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM);
        return ret;
    }

    int32_t pid = -1;
    int32_t priority = -1;

    napi_get_value_int32(env, argv[PID_INDEX], &pid);
    napi_get_value_int32(env, argv[PRIORITY_INDEX], &priority);

    BackgroundProcessManager_ProcessPriority processPriority =
        static_cast<BackgroundProcessManager_ProcessPriority>(priority);

    int retCode = OH_BackgroundProcessManager_SetProcessPriority(pid, processPriority);
    HandleErrorCode(env, retCode);
    napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_SUCCESS, &ret);
    return ret;
}

napi_value ResetProcessPriority(napi_env env, napi_callback_info info)
{
    napi_value ret;
    size_t argc = RESET_PROCESS_PRIORITY_PARAM_NUM;
    napi_value argv[RESET_PROCESS_PRIORITY_PARAM_NUM] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc != RESET_PROCESS_PRIORITY_PARAM_NUM) {
        RESSCHED_LOGE("param num error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM);
        return ret;
    }

    napi_valuetype pidType;
    napi_typeof(env, argv[PID_INDEX], &pidType);

    if (pidType != napi_number) {
        RESSCHED_LOGE("param type error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_INVALID_PARAM);
        return ret;
    }

    int32_t pid = -1;

    napi_get_value_int32(env, argv[PID_INDEX], &pid);

    int retCode = OH_BackgroundProcessManager_ResetProcessPriority(pid);
    HandleErrorCode(env, retCode);
    napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_SUCCESS, &ret);
    return ret;
}

napi_value SetPowerSaveMode(napi_env env, napi_callback_info info)
{
    napi_value ret;
    size_t argc = SET_POWER_SAVE_MODE_PARAM_NUM;
    napi_value argv[SET_POWER_SAVE_MODE_PARAM_NUM] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc != SET_POWER_SAVE_MODE_PARAM_NUM) {
        RESSCHED_LOGE("param num error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    napi_valuetype pidType;
    napi_typeof(env, argv[PID_INDEX], &pidType);

    napi_valuetype modeType;
    napi_typeof(env, argv[MODE_INDEX], &modeType);

    if (pidType != napi_number || modeType != napi_number) {
        RESSCHED_LOGE("param type error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    int32_t pid = -1;
    int32_t mode = -1;

    napi_get_value_int32(env, argv[PID_INDEX], &pid);
    napi_get_value_int32(env, argv[MODE_INDEX], &mode);

    BackgroundProcessManager_PowerSaveMode processMode =
        static_cast<BackgroundProcessManager_PowerSaveMode>(mode);

    int retCode = OH_BackgroundProcessManager_SetPowerSaveMode(pid, processMode);
    HandleErrorCode(env, retCode);
    napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_SUCCESS, &ret);
    return ret;
}

napi_value IsPowerSaveMode(napi_env env, napi_callback_info info)
{
    napi_value ret;
    size_t argc = IS_POWER_SAVE_MODE_PARAM_NUM;
    napi_value argv[IS_POWER_SAVE_MODE_PARAM_NUM] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc != IS_POWER_SAVE_MODE_PARAM_NUM) {
        RESSCHED_LOGE("param num error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    napi_valuetype pidType;
    napi_typeof(env, argv[PID_INDEX], &pidType);

    if (pidType != napi_number) {
        RESSCHED_LOGE("param type error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    int32_t pid = -1;
    napi_get_value_int32(env, argv[PID_INDEX], &pid);

    int retCode = OH_BackgroundProcessManager_IsPowerSaveMode(pid);
    HandleErrorCode(env, retCode);
    if (retCode != IS_POWER_SAVE_NOK && retCode != IS_POWER_SAVE_OK) {
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_SUCCESS, &ret);
        return ret;
    }

    napi_value promise = nullptr;
    napi_deferred deferred = nullptr;
    napi_create_promise(env, &deferred, &promise);
    bool isPowerMode = static_cast<bool>(retCode);
    napi_get_boolean(env, isPowerMode, &ret);
    napi_resolve_deferred(env, deferred, ret);
    return promise;
}

napi_value GetPowerSaveMode(napi_env env, napi_callback_info info)
{
    napi_value ret;
    size_t argc = IS_POWER_SAVE_MODE_PARAM_NUM;
    napi_value argv[IS_POWER_SAVE_MODE_PARAM_NUM] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc != IS_POWER_SAVE_MODE_PARAM_NUM) {
        RESSCHED_LOGE("param num error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    napi_valuetype pidType;
    napi_typeof(env, argv[PID_INDEX], &pidType);

    if (pidType != napi_number) {
        RESSCHED_LOGE("param type error");
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR, &ret);
        HandleErrorCode(env, ERR_BACKGROUND_PROCESS_MANAGER_PARAMETER_ERROR);
        return ret;
    }

    int32_t pid = -1;
    napi_get_value_int32(env, argv[PID_INDEX], &pid);

    int retCode = OH_BackgroundProcessManager_GetPowerSaveMode(pid);
    HandleErrorCode(env, retCode);
    if (retCode != BackgroundProcessManager_PowerSaveMode::EFFICIENCY_MODE &&
        retCode != BackgroundProcessManager_PowerSaveMode::DEFAULT_MODE) {
        napi_create_int32(env, ERR_BACKGROUND_PROCESS_MANAGER_SUCCESS, &ret);
        return ret;
    }

    napi_value promise = nullptr;
    napi_deferred deferred = nullptr;
    napi_create_promise(env, &deferred, &promise);
    napi_create_int32(env, retCode, &ret);
    napi_resolve_deferred(env, deferred, ret);
    return promise;
}

static void SetPropertyName(napi_env env, napi_value dstObj, int32_t objName, const char * propName)
{
    napi_value prop = nullptr;
    if (napi_create_int32(env, objName, &prop) == napi_ok) {
        napi_set_named_property(env, dstObj, propName, prop);
    }
}

static napi_value ProcessPriorityInit(napi_env env, napi_value exports)
{
    napi_value obj = nullptr;
    napi_create_object(env, &obj);
    SetPropertyName(env, obj, static_cast<uint32_t>(BackgroundProcessManager_ProcessPriority::PROCESS_BACKGROUND),
        "PROCESS_BACKGROUND");
    SetPropertyName(env, obj, static_cast<uint32_t>(BackgroundProcessManager_ProcessPriority::PROCESS_INACTIVE),
        "PROCESS_INACTIVE");

    napi_value powerMode = nullptr;
    napi_create_object(env, &powerMode);
    SetPropertyName(env, powerMode, static_cast<uint32_t>(BackgroundProcessManager_PowerSaveMode::EFFICIENCY_MODE),
        "EFFICIENCY_MODE");
    SetPropertyName(env, powerMode, static_cast<uint32_t>(BackgroundProcessManager_PowerSaveMode::DEFAULT_MODE),
        "DEFAULT_MODE");

    napi_property_descriptor desc[] = {
        DECLARE_NAPI_PROPERTY("ProcessPriority", obj),
        DECLARE_NAPI_PROPERTY("PowerSaveMode", powerMode),
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(*desc), desc);
    return exports;
}

static napi_value InitBackgroundProcessManagerAPi(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        DECLARE_NAPI_FUNCTION("setProcessPriority", SetProcessPriority),
        DECLARE_NAPI_FUNCTION("resetProcessPriority", ResetProcessPriority),
        DECLARE_NAPI_FUNCTION("setPowerSaveMode", SetPowerSaveMode),
        DECLARE_NAPI_FUNCTION("isPowerSaveMode", IsPowerSaveMode),
        DECLARE_NAPI_FUNCTION("getPowerSaveMode", GetPowerSaveMode),
    };

    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(desc) / sizeof(*desc), desc));
    return exports;
}

napi_value Init(napi_env env, napi_value exports)
{
    InitBackgroundProcessManagerAPi(env, exports);
    ProcessPriorityInit(env, exports);
    return exports;
}

__attribute__((constructor)) void RegisterModule(void)
{
    napi_module_register(&backgroundProcessManagerModule);
}

#ifdef __cplusplus
}
#endif
}
}