#include <cstring>
#include <iostream>
#include <random>

#include "dummy_transfer_agent.h"

namespace mooncake
{
    void DummyTransferAgent::init()
    {
        // Dummy initialization, if needed
    }

    void *DummyTransferAgent::allocateLocalMemory(size_t buffer_size)
    {
        void *address = malloc(buffer_size);
        addr_.push_back(address);
        return address;
    }
    bool DummyTransferAgent::doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        for (auto &task : transfer_tasks)
        {
            void *target_address = (void *)task.target_offset;
            transfer_status.push_back(TransferStatusEnum::COMPLETED);
            std::memcpy(target_address, task.source, task.length);
            std::string str((char *)task.source, task.length);
            LOG(INFO) << "write data to " << (void *)target_address << " with size " << task.length;
        }
        return true;
    }

    bool DummyTransferAgent::doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        transfer_status.clear();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        for (auto &task : transfer_tasks)
        {
            void *target_address = (void *)task.target_offset;
            std::memcpy(task.source, target_address, task.length);
            transfer_status.push_back(TransferStatusEnum::COMPLETED);
            std::string str((char *)task.source, task.length);
            LOG(INFO) << "read data from " << (void *)target_address << " with size " << task.length;
        }

        if (dis(gen) < 0.2)
        {
            int index = transfer_status.size() / 2;
            transfer_status[transfer_status.size() / 2] = TransferStatusEnum::FAILED;
            std::memset(transfer_tasks[index].source, 0, transfer_tasks[index].length); // 清理task.source的内容
            LOG(WARNING) << "Task failed and source content cleared, index: " << index;
            return false;
        }
        LOG(INFO) << "doRead succeed, task size: " << transfer_tasks.size();
        return true;
    }

    bool DummyTransferAgent::doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        return doWrite(transfer_tasks, transfer_status);
    }

    bool DummyTransferAgent::doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        for (size_t i = 0; i < transfer_tasks.size(); ++i)
        {
            transfer_status.push_back(TransferStatusEnum::COMPLETED);
        }
        return true;
    }

    BatchID DummyTransferAgent::submitTransfersAsync(const std::vector<TransferRequest> &transfer_tasks)
    {
        LOG(INFO) << "task size: " << transfer_tasks.size();
        return 0;
    }

    void DummyTransferAgent::monitorTransferStatus(BatchID batch_id, size_t task_count, std::vector<TransferStatusEnum> &transfer_status)
    {
        LOG(INFO) << "the batch_id: " << batch_id << " has " << task_count << " task status size: " << transfer_status.size();
        return;
    }
} // namespace mooncake