#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

#include "rdma_transfer_agent.h"

namespace mooncake
{
    // for transfer engine
    #define BASE_ADDRESS_HINT (0x40000000000)
    static std::string getHostname()
    {
        char hostname[256];
        if (gethostname(hostname, 256))
        {
            PLOG(ERROR) << "Failed to get hostname";
            return "";
        }
        return hostname;
    }

    DEFINE_string(local_server_name, getHostname(), "Local server name for segment discovery");
    DEFINE_string(metadata_server, "optane21:2379", "etcd server host address");
    DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_0\"], []], \"cpu:1\": [[\"mlx5_0\"], []]}", "NIC priority matrix");
    DEFINE_string(
        nic_priority_matrix_dummy,
        "{\"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]], \"cpu:1\": [[\"mlx5_3\"], [\"mlx5_2\"]]}",
        "NIC priority matrix");

    DEFINE_string(segment_id, "optane08", "Segment ID to access data");
    DEFINE_int32(batch_size_dummy, 128, "Batch size");

    
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
        auto metadata_client = std::make_shared<TransferMetadata>(FLAGS_metadata_server);
        LOG_ASSERT(metadata_client);

        auto nic_priority_matrix = loadNicPriorityMatrix(FLAGS_nic_priority_matrix);
        transfer_engine_ = std::make_unique<TransferEngine>(metadata_client);

        void **args = (void **)malloc(2 * sizeof(void *));
        args[0] = (void *)nic_priority_matrix.c_str();
        args[1] = nullptr;

        const string &connectable_name = FLAGS_local_server_name;
        transfer_engine_->init(FLAGS_local_server_name.c_str(), connectable_name.c_str(), 12345);
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
            if (failed) {
                LOG(ERROR) << "doTransfers failed";
                return false;
            }
        }
        ret = rdma_engine_->freeBatchID(batch_id);
        LOG(ERROR) << "freeBatchID ret: " << ret;
        return (ret == 0) ? true : false;
    }
} // namespace mooncake