// Copyright 2024 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <vector>

#include "transfer_engine.h"
#include "transport/transport.h"
#include "types.h"

namespace mooncake {
using TransferCallback =
    std::function<void(const std::vector<TransferStatusEnum> &)>;
class TransferAgent {
   public:
    TransferAgent() = default;
    virtual ~TransferAgent() = default;
    virtual void init() = 0;
    virtual void *allocateLocalMemory(size_t buffer_size) = 0;
    virtual SegmentId openSegment(const std::string &segment_name) = 0;

    virtual bool doWrite(const std::vector<TransferRequest> &transfer_tasks,
                         std::vector<TransferStatusEnum> &transfer_status) = 0;
    virtual bool doRead(const std::vector<TransferRequest> &transfer_tasks,
                        std::vector<TransferStatusEnum> &transfer_status) = 0;
    virtual bool doReplica(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status) = 0;
    virtual bool doTransfers(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status) = 0;
    virtual BatchID submitTransfersAsync(
        const std::vector<TransferRequest> &transfer_tasks) = 0;

    virtual void monitorTransferStatus(
        BatchID batch_id, size_t task_count,
        std::vector<TransferStatusEnum> &transfer_status) = 0;
};

}  // namespace mooncake