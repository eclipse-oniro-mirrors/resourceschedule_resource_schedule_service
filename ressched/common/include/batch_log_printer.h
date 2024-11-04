/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef RESOURCE_SCHEDULE_SERVICE_RESSCHED_COMMON_BATCH_LOG_PRINTER_H
#define RESOURCE_SCHEDULE_SERVICE_RESSCHED_COMMON_BATCH_LOG_PRINTER_H

#include <string>
#include <unistd.h>

#include "ffrt.h"
#include "single_instance.h"

namespace OHOS {
namespace ResourceSchedule {
class BatchLogPrinter {
    DECLARE_SINGLE_INSTANCE_BASE(BatchLogPrinter);
public:
    BatchLogPrinter();
    void SubmitLog(const std::string& log);
private:
    void RecordLog(const std::string& log, const int64_t& timestamp);
    void GetBatch(std::vector<std::string>& batchLogs);
    std::shared_ptr<ffrt::queue> logQueue_;
    std::vector<std::string> allLogs_;
    int64_t lastPrintTimestamp_;
};
} // namespace ResourceSchedule
} // namespace OHOS
#endif // RESOURCE_SCHEDULE_SERVICE_RESSCHED_COMMON_BATCH_LOG_PRINTER_H
