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

#ifndef RESSCHED_SERVICES_RESSCHEDMGR_RESSCHEDFWK_INCLUDE_PLUGIN_MGR_H
#define RESSCHED_SERVICES_RESSCHEDMGR_RESSCHEDFWK_INCLUDE_PLUGIN_MGR_H

#include <atomic>
#include <functional>
#include <list>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <set>
#include <unordered_set>
#include "datetime_ex.h"
#include "event_handler.h"
#include "config_reader.h"
#include "plugin_switch.h"
#include "plugin.h"
#include "nocopyable.h"
#include "res_data.h"
#include "single_instance.h"
#include "config_info.h"
#include "ffrt.h"
#ifdef RESOURCE_SCHEDULE_SERVICE_WITH_EXT_RES_ENABLE
#include "res_type.h"
#endif

namespace OHOS {
namespace ResourceSchedule {
using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;
using OnPluginInitFunc = bool (*)(std::string&);
using OnDispatchResourceFunc = void (*)(const std::shared_ptr<ResData>&);
using OnDeliverResourceFunc = int32_t (*)(const std::shared_ptr<ResData>&);
using OnDumpFunc = void (*)(const std::vector<std::string>&, std::string&);
using OnPluginDisableFunc = void (*)();
using OnIsAllowedAppPreloadFunc = bool (*)(const std::string&, int32_t preloadMode);
using GetExtMultiConfigFunc = int32_t (*)(int32_t, std::vector<std::string>&);

constexpr int32_t DISPATCH_TIME_OUT = 50; // ms
constexpr int32_t DISPATCH_TIME_OUT_US = DISPATCH_TIME_OUT * 1000; // us
constexpr int32_t PLUGIN_STAT_MAX_USE_COUNT = 1000;

struct PluginStat {
    uint64_t totalTime;
    uint64_t useCount;
    std::list<TimePoint> timeOutTime;

    inline void Update(int32_t costTime)
    {
        if (costTime > 0 && costTime < DISPATCH_TIME_OUT_US) {
            if (totalTime + (uint32_t)costTime < totalTime) {
                totalTime = (uint32_t)costTime;
                useCount = 1;
            } else {
                totalTime += (uint32_t)costTime;
                useCount += 1;
            }
        }
    }

    inline uint64_t AverageTime()
    {
        return (useCount > 0) ? (totalTime / useCount) : 0;
    }
};

struct PluginLib {
    std::shared_ptr<void> handle = nullptr;
    OnPluginInitFunc onPluginInitFunc_;
    OnDispatchResourceFunc onDispatchResourceFunc_;
    OnDeliverResourceFunc onDeliverResourceFunc_;
    OnDumpFunc onDumpFunc_;
    OnPluginDisableFunc onPluginDisableFunc_;
};

class PluginMgr {
    DECLARE_SINGLE_INSTANCE_BASE(PluginMgr);

public:
    ~PluginMgr();

    /**
     * Init pluginmanager, load xml config file, construct plugin instances.
     *
     * @param isRssExe Calling service is resource schedule executor.
     */
    void Init(bool isRssExe = false);

    /**
     * Disable all plugins, maybe service exception happens or stopped.
     */
    void Stop();

    /**
     * receive all reported resource data, then dispatch all plugins.
     *
     * @param resData Reported resource data.
     */
    void DispatchResource(const std::shared_ptr<ResData>& resData);

    /**
     * receive all reported sync resource data, then deliver to plugins.
     *
     * @param resData Reported resource data.
     */
    int32_t DeliverResource(const std::shared_ptr<ResData>& resData);

    /**
     * Subscribe resource type from plugin.
     *
     * @param pluginLib The lib name of plugin.
     * @param resType interested in resource type.
     */
    void SubscribeResource(const std::string& pluginLib, uint32_t resType);

    /**
     * Unsubscribe resource type from plugin.
     *
     * @param pluginLib The lib name of plugin.
     * @param resType interested in resource type.
     */
    void UnSubscribeResource(const std::string& pluginLib, uint32_t resType);

    /**
     * Unsubscribe resource type from plugin.
     *
     * @param pluginLib The lib name of plugin.
     */
    void UnSubscribeAllResources(const std::string& pluginLib);

    /**
     * Subscribe sync resource type from plugin.
     *
     * @param pluginLib The lib name of plugin.
     * @param resType interested in resource type.
     */
    void SubscribeSyncResource(const std::string& pluginLib, uint32_t resType);

    /**
     * Unsubscribe sync resource type from plugin.
     *
     * @param pluginLib The lib name of plugin.
     * @param resType interested in resource type.
     */
    void UnSubscribeSyncResource(const std::string& pluginLib, uint32_t resType);

    /**
     * Kill process by pid.
     *
     * @param payload process message.
     */
    void KillProcessByPid(const nlohmann::json& payload, std::string killClientInitiator);

    void DumpAllPlugin(std::string &result);

    void DumpAllPluginConfig(std::string &result);

    void DumpOnePlugin(std::string &result, std::string pluginName, std::vector<std::string>& args);

    std::string DumpInfoFromPlugin(std::string& result, std::string libPath, std::vector<std::string>& args);

    void DumpHelpFromPlugin(std::string& result);

    PluginConfig GetConfig(const std::string& pluginName, const std::string& configName);

    void RemoveConfig(const std::string& pluginName, const std::string& configName);

    void SetResTypeStrMap(const std::map<uint32_t, std::string>& resTypeStr);

    std::shared_ptr<PluginLib> GetPluginLib(const std::string& libPath);

    void GetConfigContent(int32_t configIdx, const std::string& configPath, std::vector<std::string>& contents);

    /**
     * Get config reader xml file.
     *
     * @return config reader xml file string.
     */
    std::vector<std::string> GetConfigReaderStr();

    /**
     * Get plugin switch xml file.
     *
     * @return plugin switch xml file string.
     */
    std::vector<std::string> GetPluginSwitchStr();

    /**
     * Parse config reader xml file.
     *
     * @param configStr The string of config reader xml file.
     */
    void ParseConfigReader(const std::vector<std::string>& configStrs);

    /**
     * Parse plugin switch xml file.
     *
     * @param switchStr The string of plugin switch xml file.
     * @param isRssExe is calling service resource_schedule_executor.
     */
    void ParsePluginSwitch(const std::vector<std::string>& switchStrs, bool isRssExe = false);

    /**
     * set plugin blocked time.
     *
     * @param time over time will be judge blocked
     */
    void SetBlockedTime(const int64_t time);

    void SetLinkJumpOptSet(const std::unordered_set<std::string> linkJumpOptSet);

    bool GetLinkJumpOptConfig(const std::string& bundleName, bool& isAllowedLinkJump);

    void GetResTypeList(std::set<uint32_t>& resTypeList);
private:
    PluginMgr() = default;
    void OnDestroy();
    void LoadPlugin();
    void LoadGetExtConfigFunc();
    std::shared_ptr<PluginLib> LoadOnePlugin(const PluginInfo& info);
    void UnLoadPlugin();
    void ClearResource();
    void DispatchResourceToPluginSync(const std::list<std::string>& pluginList,
        const std::shared_ptr<ResData>& resData);
#ifdef RESOURCE_SCHEDULE_SERVICE_WITH_FFRT_ENABLE
    void DispatchResourceToPluginAsync(const std::list<std::string>& pluginList,
        const std::shared_ptr<ResData>& resData);
    void HandlePluginTimeout(const std::string& pluginLib);
    void EnablePluginIfResume(const std::string& pluginLib);
    void RecordRinningStat(std::string pluginLib, bool isRunning);
    bool IsPluginRunning(const std::string& pluginLib);
#endif
    void RepairPlugin(TimePoint endTime, const std::string& pluginLib, PluginLib libInfo);
    void RemoveDisablePluginHandler();
    void DumpPluginInfoAppend(std::string &result, PluginInfo info);
    bool GetPluginListByResType(uint32_t resType, std::list<std::string>& pluginList);
    bool CheckRealPath(const std::string& partialPath, std::string& fullPath);
    std::vector<std::string> GetAllRealConfigPath(const std::string& configName);
    std::string BuildDispatchTrace(const std::shared_ptr<ResData>& resData, std::string& libNameAll,
        const std::string& funcName, std::list<std::string>& pluginList);
    bool CheckValidPlugin(const PluginInfo& info, void* pluginHandle, std::string& errorMsg,
        OnPluginInitFunc& onPluginInitFunc, OnPluginDisableFunc& onPluginDisableFunc);
#ifdef RESOURCE_SCHEDULE_SERVICE_WITH_EXT_RES_ENABLE
    int32_t GetExtTypeByResPayload(const std::shared_ptr<ResData>& resData);
#endif
    std::list<std::string> SortPluginList(const std::list<std::string>& pluginList);
    std::string GetStrFromResTypeStrMap(uint32_t resType);
    class InnerTimeUtil {
    public:
        InnerTimeUtil(const std::string& func, const std::string& plugin);
        ~InnerTimeUtil();
    private:
        TimePoint beginTime_;
        std::string functionName_;
        std::string pluginName_;
    };

    // plugin crash 3 times in 60s, will be disable forever
    const int32_t MAX_PLUGIN_TIMEOUT_TIMES = 3;
    const int32_t DISABLE_PLUGIN_TIME = 60000;
    const int32_t DUMP_ONE_STRING_SIZE = 32;
    int64_t pluginBlockTime = 10 * 60 * 1000 * 1000;
    std::unique_ptr<ConfigReader> configReader_ = nullptr;
    std::unique_ptr<PluginSwitch> pluginSwitch_ = nullptr;

    std::map<std::string, PluginLib> pluginLibMap_;

    // mutex for resTypeMap_
    std::mutex resTypeMutex_;
    // mutex for resTypeLibSyncMap_
    std::mutex resTypeSyncMutex_;
    // mutex for resTypeStrMap_
    std::mutex resTypeStrMutex_;
    std::mutex pluginMutex_;
    ffrt::mutex dispatcherHandlerMutex_;
    std::mutex libPathMutex_;
    std::mutex linkJumpOptMutex_;
    std::map<uint32_t, std::list<std::string>> resTypeLibMap_;
    std::map<uint32_t, std::string> resTypeLibSyncMap_;
    std::map<uint32_t, std::string> resTypeStrMap_;
    std::unordered_set<std::string> linkJumpOptSet_;

#ifdef RESOURCE_SCHEDULE_SERVICE_WITH_FFRT_ENABLE
    std::map<std::string, std::shared_ptr<ffrt::queue>> dispatchers_;
    ffrt::mutex runningStatsMutex_;
    std::map<std::string, bool> runningStats_;
    std::unordered_set<std::string> disablePlugins_;
#else
    std::shared_ptr<AppExecFwk::EventHandler> dispatcher_ = nullptr;
#endif
    std::atomic<bool> isInit = {false};

    std::map<std::string, PluginStat> pluginStat_;
    GetExtMultiConfigFunc getExtMultiConfigFunc_ = nullptr;
};
} // namespace ResourceSchedule
} // namespace OHOS

#endif // RESSCHED_SERVICES_RESSCHEDMGR_RESSCHEDFWK_INCLUDE_PLUGIN_MGR_H
