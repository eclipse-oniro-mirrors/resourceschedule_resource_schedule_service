/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <unistd.h>
#include "accesstoken_kit.h"
#include "ipc_skeleton.h"
#include "nativetoken_kit.h"
#include "res_sched_client.h"
#include "token_setproc.h"

const static int32_t PARAMETERS_NUM_MIN                      = 2;
const static int32_t PARAMETERS_NUM_MIN_KILL_PROCESS         = 4;
const static int32_t PARAMETERS_NUM_KILL_PROCESS_PROCESSNAME = 5;
const static int32_t PARAMETERS_NUM_REPORT_DATA = 6;
const static int32_t PARAMETERS_NUM_REQUEST_TEST = 5;
const static int32_t PID_INDEX = 3;
const static int32_t RES_TYPE_INDEX = 4;
const static int32_t UID_INDEX = 2;
const static int32_t VALUE_INDEX = 5;
const static int32_t REQUEST_COUNT_INDEX = 2;
const static int32_t REQUEST_TIME_INDEX = 3;
const static int32_t REQUEST_UID_INDEX = 4;
const static int32_t DEFAULT_TYPE = 1;
const static int32_t DEFAULT_VALUE = 1;

static void MockProcess(int32_t uid)
{
    static const char *perms[] = {
        "ohos.permission.DISTRIBUTED_DATASYNC",
        "ohos.permission.REPORT_RESOURCE_SCHEDULE_EVENT"
    };
    uint64_t tokenId;
    NativeTokenInfoParams infoInstance = {
        .dcapsNum = 0,
        .permsNum = sizeof(perms) / sizeof(perms[0]),
        .aclsNum = 0,
        .dcaps = nullptr,
        .perms = perms,
        .acls = nullptr,
        .processName = "samgr",
        .aplStr = "system_core",
    };
    tokenId = GetAccessTokenId(&infoInstance);
    SetSelfTokenID(tokenId);
    setuid(uid);
    OHOS::Security::AccessToken::AccessTokenKit::ReloadNativeTokenInfo();
}

static int64_t GetNowTime()
{
    time_t now;
    (void)time(&now);
    if (static_cast<int64_t>(now) < 0) {
        std::cout << "Get now time error " << std::endl;
        return 0;
    }
    auto tarEndTimePoint = std::chrono::system_clock::from_time_t(now);
    auto tarDuration = std::chrono::duration_cast<std::chrono::microseconds>(tarEndTimePoint.time_since_epoch());
    int64_t tarDate = tarDuration.count();
    if (tarDate < 0) {
        std::cout << "tarDuration is less than 0 " << std::endl;
        return -1;
    }
    return static_cast<int64_t>(tarDate);
}

static void KillProcess(int32_t argc, char *argv[])
{
    if (argc < PARAMETERS_NUM_MIN_KILL_PROCESS) {
        return;
    }
    int32_t uid = atoi(argv[PARAMETERS_NUM_MIN]);
    MockProcess(uid);
    std::unordered_map<std::string, std::string> mapPayload;
    mapPayload["pid"] = argv[PARAMETERS_NUM_MIN_KILL_PROCESS - 1];
    if (argc >= PARAMETERS_NUM_KILL_PROCESS_PROCESSNAME) {
        mapPayload["processName"] = argv[PARAMETERS_NUM_KILL_PROCESS_PROCESSNAME - 1];
    }
    int32_t res = OHOS::ResourceSchedule::ResSchedClient::GetInstance().KillProcess(mapPayload);
    std::cout << "kill result:" << res << std::endl;
}

static void ReportData(int32_t argc, char *argv[])
{
    if (argc != PARAMETERS_NUM_REPORT_DATA) {
        return;
    }
    int32_t uid = atoi(argv[UID_INDEX]);
    MockProcess(uid);
    int32_t pid = atoi(argv[PID_INDEX]);
    int32_t resType = atoi(argv[RES_TYPE_INDEX]);
    int32_t value = atoi(argv[VALUE_INDEX]);
    std::unordered_map<std::string, std::string> mapPayload;
    mapPayload["uid"] = uid;
    mapPayload["pid"] = pid;
    OHOS::ResourceSchedule::ResSchedClient::GetInstance().ReportData(resType, value, mapPayload);
    std::cout << "success passing on pid = " << pid << std::endl;
}

static void RequestTest(int32_t argc, char *argv[])
{
    if (argc != PARAMETERS_NUM_REQUEST_TEST) {
        return;
    }
    int32_t requestCount = atoi(argv[REQUEST_COUNT_INDEX]);
    int32_t requestTime = atoi(argv[REQUEST_TIME_INDEX]);
    int32_t uid = atoi(argv[REQUEST_UID_INDEX]);
    MockProcess(uid);
    std::unordered_map<std::string, std::string> mapPayload;
    int64_t startTime = GetNowTime();
    int32_t count = 0;
    while (count < requestCount && GetNowTime() - startTime < requestTime) {
        count++;
        OHOS::ResourceSchedule::ResSchedClient::GetInstance().ReportData(DEFAULT_TYPE, DEFAULT_VALUE, mapPayload);
    }
    std::cout << "success RequestTest " << uid << std::endl;
}

int32_t main(int32_t argc, char *argv[])
{
    if (!(argc >= PARAMETERS_NUM_MIN && argv)) {
        std::cout << "error parameters";
        return 0;
    }
    char* function = argv[1];
    if (strcmp(function, "KillProcess") == 0) {
        KillProcess(argc, argv);
    } else if (strcmp(function, "ReportData") == 0) {
        ReportData(argc, argv);
    } else if (strcmp(function, "RequestTest") == 0) {
        RequestTest(argc, argv);
    } else {
        std::cout << "error parameters";
    }
    return 0;
}