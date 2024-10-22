#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

#include "config.h"
#include "rdma_transfer_agent.h"

namespace mooncake
{
// for transfer engine
#define BASE_ADDRESS_HINT (0x40000000000)

    static void *allocateMemoryPool(size_t size, int socket_id)
    {
        return numa_alloc_onnode(size, socket_id);
    }

    static void freeMemoryPool(void *addr, size_t size)
    {
        numa_free(addr, size);
    }

    std::string loadNicPriorityMatrix(const std::string &path)
    {
        std::ifstream file(path);
        if (file.is_open())
        {
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();
            return content;
        }
        else
        {
            return path;
        }
    }

    RdmaTransferAgent::RdmaTransferAgent() : rdma_engine_(nullptr) {}

    RdmaTransferAgent::~RdmaTransferAgent()
    {
        for (size_t i = 0; i < addr_.size(); ++i)
        {
            rdma_engine_->unregisterLocalMemory(addr_[i]);
        }
        freeMemoryPool((void *)BASE_ADDRESS_HINT, dram_buffer_size_);
    }

    void RdmaTransferAgent::init()
    {
        // 使用 ConfigManager 获取配置值
        auto &configManager = ConfigManager::getInstance();
        auto local_server_name = configManager.get("local_server_name");
        auto metadata_server = configManager.get("metadata_server");
        auto nic_priority_matrix = configManager.get("nic_priority_matrix");

        auto metadata_client = std::make_shared<TransferMetadata>(metadata_server);
        LOG_ASSERT(metadata_client);

        auto nic_priority_matrix_content = loadNicPriorityMatrix(nic_priority_matrix);
        transfer_engine_ = std::make_unique<TransferEngine>(metadata_client);

        void **args = (void **)malloc(2 * sizeof(void *));
        args[0] = (void *)nic_priority_matrix_content.c_str();
        args[1] = nullptr;

        const string &connectable_name = local_server_name;
        transfer_engine_->init(local_server_name.c_str(), connectable_name.c_str(), 12345);
        rdma_engine_ = static_cast<RdmaTransport *>(transfer_engine_->installOrGetTransport("rdma", args));
        LOG_ASSERT(transfer_engine_);
    }

    SegmentId RdmaTransferAgent::openSegment(const std::string &segment_name)
    {
        return transfer_engine_->openSegment(segment_name.c_str());
    }

    void *RdmaTransferAgent::allocateLocalMemory(size_t buffer_size)
    {
        void *address = allocateMemoryPool(buffer_size, 0);
        addr_.push_back(address);
        int rc = transfer_engine_->registerLocalMemory(address, buffer_size, "cpu:" + std::to_string(0));
        LOG_ASSERT(!rc);
        return address;
    }

    bool RdmaTransferAgent::doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        LOG(INFO) << "begin write data, task size: " << transfer_tasks.size();
        int ret = doTransfers(transfer_tasks, transfer_status);
        LOG(INFO) << "finish write data, task size: " << transfer_tasks.size();
        return (ret == 0) ? true : false;
    }

    bool RdmaTransferAgent::doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        LOG(INFO) << "begin read data, task size: " << transfer_tasks.size();
        int ret = doTransfers(transfer_tasks, transfer_status);
        LOG(INFO) << "finish read data, task size: " << transfer_tasks.size();
        return (ret == 0) ? true : false;
    }

    bool RdmaTransferAgent::doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        return doTransfers(transfer_tasks, transfer_status);
    }

    bool RdmaTransferAgent::doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
    {
        transfer_status.resize(transfer_tasks.size());
        auto batch_id = rdma_engine_->allocateBatchID(transfer_tasks.size());
        int ret = rdma_engine_->submitTransfer(batch_id, transfer_tasks);
        LOG_ASSERT(!ret);

        for (size_t task_id = 0; task_id < transfer_tasks.size(); ++task_id)
        {
            bool completed = false, failed = false;
            TransferStatus status;
            while (!completed && !failed)
            {
                int ret = rdma_engine_->getTransferStatus(batch_id, task_id, status);
                LOG_ASSERT(!ret);
                if (status.s == TransferStatusEnum::COMPLETED)
                    completed = true;
                else if (status.s == TransferStatusEnum::FAILED)
                    failed = true;
            }
            transfer_status[task_id] = status.s;
            if (failed)
            {
                LOG(ERROR) << "doTransfers failed";
                return false;
            }
        }
        ret = rdma_engine_->freeBatchID(batch_id);
        LOG(ERROR) << "freeBatchID ret: " << ret;
        return (ret == 0) ? true : false;
    }

    // 异步提交传输请求
    BatchID RdmaTransferAgent::submitTransfersAsync(const std::vector<TransferRequest> &transfer_tasks)
    {
        auto batch_id = rdma_engine_->allocateBatchID(transfer_tasks.size());
        int ret = rdma_engine_->submitTransfer(batch_id, transfer_tasks);
        if (ret != 0)
        {
            LOG(ERROR) << "Failed to submit transfer, batch_id: " << batch_id;
            rdma_engine_->freeBatchID(batch_id);
            return getError(ERRNO::TRANSFER_FAIL);
        }
        return batch_id;
    }

    void RdmaTransferAgent::monitorTransferStatus(BatchID batch_id, size_t task_count, std::vector<TransferStatusEnum> &transfer_status)
    {
        // std::vector<TransferStatusEnum> transfer_status(task_count, TransferStatusEnum::PENDING);
        size_t completed_count = 0;

        while (completed_count < task_count)
        {
            for (size_t task_id = 0; task_id < task_count; ++task_id)
            {
                if (transfer_status[task_id] == TransferStatusEnum::PENDING)
                {
                    TransferStatus status;
                    int ret = rdma_engine_->getTransferStatus(batch_id, task_id, status);
                    if (ret == 0)
                    {
                        if (status.s == TransferStatusEnum::COMPLETED || status.s == TransferStatusEnum::FAILED)
                        {
                            transfer_status[task_id] = status.s;
                            ++completed_count;
                        }
                        LOG(ERROR) << "the task_id: " << task_id << " , status: " << status.s;
                    }
                    else
                    {
                        LOG(ERROR) << "Failed to get transfer status, batch_id: " << batch_id << ", task_id: " << task_id;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 避免频繁轮询
        }

        rdma_engine_->freeBatchID(batch_id);
        // callback(transfer_status); // 调用回调函数，通知上层所有传输已完成
    }

} // namespace mooncake