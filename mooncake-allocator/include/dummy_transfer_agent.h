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

#include "transfer_agent.h"
#include "types.h"

namespace mooncake {

class DummyTransferAgent : public TransferAgent {
   public:
    void init() override;
    void *allocateLocalMemory(size_t buffer_size) override;
    SegmentId openSegment(const std::string &segment_name) override;
    bool doWrite(const std::vector<TransferRequest> &transfer_tasks,
                 std::vector<TransferStatusEnum> &transfer_status) override;
    bool doRead(const std::vector<TransferRequest> &transfer_tasks,
                std::vector<TransferStatusEnum> &transfer_status) override;
    bool doReplica(const std::vector<TransferRequest> &transfer_tasks,
                   std::vector<TransferStatusEnum> &transfer_status) override;
    bool doTransfers(const std::vector<TransferRequest> &transfer_tasks,
                     std::vector<TransferStatusEnum> &transfer_status) override;
    BatchID submitTransfersAsync(
        const std::vector<TransferRequest> &transfer_tasks) override;
    void monitorTransferStatus(
        BatchID batch_id, size_t task_count,
        std::vector<TransferStatusEnum> &transfer_status) override;

   private:
    std::vector<void *> addr_;  // 本地存储
};
}  // namespace mooncake