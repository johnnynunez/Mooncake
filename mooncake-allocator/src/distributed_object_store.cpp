#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include "distributed_object_store.h"

namespace mooncake
{
    // for transfer engine
    #define BASE_ADDRESS_HINT (0x40000000000)

    DEFINE_string(local_server_name, getHostname(), "Local server name for segment discovery");
    DEFINE_string(metadata_server, "optane21:2379", "etcd server host address");
    // DEFINE_string(metadata_server_dummy, "optane21:12345", "etcd server host address");
    // DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_2\"], []], \"cpu:1\": [[\"mlx5_2\"], []]}", "NIC priority matrix");
    DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_0\"], []], \"cpu:1\": [[\"mlx5_0\"], []]}", "NIC priority matrix");
    DEFINE_string(
        nic_priority_matrix_dummy,
        "{\"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]], \"cpu:1\": [[\"mlx5_3\"], [\"mlx5_2\"]]}",
        "NIC priority matrix");

    DEFINE_string(segment_id, "optane08", "Segment ID to access data");
    DEFINE_int32(batch_size_dummy, 128, "Batch size");

    // for store
    DEFINE_int32(shard_size, 1024 * 64, "Shard size");
    DEFINE_int32(max_trynum, 10, "max try number");

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

    DistributedObjectStore::DistributedObjectStore() : replica_allocator_(FLAGS_shard_size), allocation_strategy_(nullptr), max_trynum_(FLAGS_max_trynum)
    {
        std::cout << "create the DistributedObjectStore!" << std::endl;
        transferEngineInit();
    }

    DistributedObjectStore::~DistributedObjectStore()
    {

        for (size_t i = 0; i < addr_.size(); ++i)
        {
            transfer_engine_->unregisterLocalMemory(addr_[i]);
        }
        freeMemoryPool((void *)BASE_ADDRESS_HINT, dram_buffer_size_);
    }

    void *DistributedObjectStore::allocateLocalMemory(size_t buffer_size)
    {
        void *address = allocateMemoryPool(buffer_size, 0);
        addr_.push_back(address);
        int rc = transfer_engine_->registerLocalMemory(address, buffer_size, "cpu:" + std::to_string(0));
        LOG_ASSERT(!rc);
        return address;
    }

    void DistributedObjectStore::transferEngineInit()
    {
        auto metadata_client = std::make_shared<TransferMetadata>(FLAGS_metadata_server);
        LOG_ASSERT(metadata_client);

        // FLAGS_nic_priority_matrix 是一个参数
        auto nic_priority_matrix = loadNicPriorityMatrix(FLAGS_nic_priority_matrix);
        transfer_engine_ = std::make_unique<TransferEngine>(metadata_client);

        void **args = (void **)malloc(2 * sizeof(void *));
        args[0] = (void *)nic_priority_matrix.c_str();
        args[1] = nullptr;

        const string &connectable_name = FLAGS_local_server_name;
        transfer_engine_->init(FLAGS_local_server_name.c_str(), connectable_name.c_str(), 12345);
        rdma_engine_ = static_cast<RdmaTransport *>(transfer_engine_->installOrGetTransport("rdma", args));

        LOG_ASSERT(transfer_engine_);

        {
            // 注册segment
            auto segment_id = transfer_engine_->openSegment(FLAGS_segment_id.c_str());
            // 注册远端地址
            registerBuffer(segment_id, (size_t)BASE_ADDRESS_HINT, dram_buffer_size_);
        }
    }

    uint64_t DistributedObjectStore::registerBuffer(SegmentId segment_id, size_t base, size_t size)
    {
        return replica_allocator_.registerBuffer(segment_id, base, size);
    }

    void DistributedObjectStore::unregisterBuffer(SegmentId segment_id, uint64_t index)
    {
        std::vector<std::shared_ptr<BufHandle>> need_reassign_buffers = replica_allocator_.unregister(segment_id, index);
        replica_allocator_.recovery(need_reassign_buffers, allocation_strategy_);
    }

    void DistributedObjectStore::updateReplicaStatus(const std::vector<TransferRequest> &requests, const std::vector<TransferStatusEnum> &status,
                                                     const std::string key, const Version version, ReplicaInfo &replica_info)
    {

        bool ifCompleted = true;
        size_t shard_size = replica_allocator_.getShardSize();
        int handle_index = 0;
        uint64_t length = 0;
        std::unordered_set<int> failed_index;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            LOG(INFO) << "request index: " << i << ", handle index: " << handle_index;
            if (status[i] != TransferStatusEnum::COMPLETED)
            {
                replica_info.handles[handle_index]->status = BufStatus::FAILED;
                failed_index.insert(handle_index);
                replica_allocator_.updateStatus(key, ReplicaStatus::PARTIAL, replica_info.replica_id, version);
                LOG(WARNING) << "handle " << i << " is failed";
                ifCompleted = false;
            }
            else
            {
                // 如果这个handle中部分写入失败，即使后面request在此handle中是成功写入的，也不应该置为COMPLETE
                if (failed_index.find(handle_index) == failed_index.end())
                {
                    replica_info.handles[handle_index]->status = BufStatus::COMPLETE;
                }
            }
            length += requests[i].length;
            handle_index = length / shard_size;
        }
        if (ifCompleted)
        {
            LOG(INFO) << "the key " << key << " , replica " << replica_info.replica_id << " is completed";
            replica_allocator_.updateStatus(key, ReplicaStatus::COMPLETE, replica_info.replica_id, version);
        }
    }

    TaskID DistributedObjectStore::put(
        ObjectKey key,
        std::vector<void *> ptrs,
        std::vector<void *> sizes,
        ReplicateConfig config)
    {
        std::vector<TransferRequest> requests;
        int replica_num = config.replica_num;
        int succeed_num = 0; // 只用于日志记录
        uint64_t total_size = calculateObjectSize(sizes);
        if (total_size == 0)
        {
            LOG(WARNING) << "the size is 0";
            return getError(ERRNO::INVALID_PARAMS);
        }
        bool first_add = true;
        Version version = 0;
        // 不管是否已经存在这个key， 都会创建一个新的version 添加此key对应的内容
        if (replica_allocator_.ifExist(key) == true)
        {
            LOG(WARNING) << "the key has existed: " << key;
        }
        for (int index = 0; index < replica_num; ++index)
        {
            ReplicaInfo replica_info;
            if (first_add)
            {
                version = replica_allocator_.addOneReplica(key, replica_info, -1, total_size, allocation_strategy_);
            }
            else
            {
                version = replica_allocator_.addOneReplica(key, replica_info, version, -1, allocation_strategy_);
            }
            if (version < 0)
            {
                LOG(ERROR) << "fail put object " << key << ", size: " << total_size << " , replica num : " << replica_num;
                break;
            }
            uint32_t trynum = 0;
            requests.clear();
            generateWriteTransferRequests(replica_info, ptrs, sizes, requests);
            for (; trynum < max_trynum_; ++trynum)
            {
                std::vector<TransferStatusEnum> status;
                bool success = doWrite(requests, status);
                // bool success = doDummyWrite(requests, status);
                if (success)
                { // update 状态
                    assert(requests.size() == status.size());
                    updateReplicaStatus(requests, status, key, version, replica_info);
                    first_add = false;
                    succeed_num++;
                    break;
                }
                // 尝试重新分配空间
                replica_info.reset();
                replica_allocator_.reassignReplica(key, version, index, replica_info);
            }
        }
        if (first_add == true)
        { // 没有一个是成功的
            for (int index = 0; index < replica_num; ++index)
            {
                ReplicaInfo ret;
                replica_allocator_.removeOneReplica(key, ret, version);
            }
            LOG(WARNING) << "no one replica is succeed when put, key: " << key << ",replica_num: " << replica_num;
            return getError(ERRNO::WRITE_FAIL);
        }
        LOG(INFO) << "put object is succeed, key: " << key << " , succeed num: " << succeed_num << ", needed replica num: " << replica_num;
        return version;
    }

    TaskID DistributedObjectStore::get(
        ObjectKey key,
        std::vector<void *> ptrs,
        std::vector<void *> sizes,
        Version min_version,
        size_t offset)
    {
        std::vector<TransferRequest> transfer_tasks;
        bool success = false;
        uint32_t trynum = 0;
        Version ver = 0;

        std::vector<TransferStatusEnum> status;
        ReplicaInfo replica_info;
        ver = replica_allocator_.getOneReplica(key, replica_info, min_version, allocation_strategy_);
        if (ver < 0)
        {
            LOG(ERROR) << "cannot get replica, key: " << key;
            return ver;
        }
        generateReadTransferRequests(replica_info, offset, ptrs, sizes, transfer_tasks);
        if (ptrs.size() == 0 || transfer_tasks.size() == 0)
        {
            LOG(ERROR) << "ptrs.size() == 0 || transfer_tasks.size() == 0";
            return getError(ERRNO::INVALID_READ);
        }
        while (!success && trynum < max_trynum_)
        {
            success = doRead(transfer_tasks, status);
            // success = doDummyRead(transfer_tasks, status);
            ++trynum;
            LOG(WARNING) << "try agin, trynum:" << trynum << ", key: " << key;
        }
        if (trynum == max_trynum_)
        {
            LOG(ERROR) << "read data failed, try maxnum: " << max_trynum_ << ",key: " << key;
            return getError(ERRNO::INVALID_READ);
        }
        return ver;
    }

    TaskID DistributedObjectStore::remove(ObjectKey key, Version version)
    {
        ReplicaInfo info;
        Version ver;
        if (replica_allocator_.ifExist(key) != true)
        {
            LOG(WARNING) << "the key isn't existed: " << key;
            return getError(ERRNO::INVALID_KEY);
        }
        ver = replica_allocator_.removeOneReplica(key, info, version);
        while (replica_allocator_.removeOneReplica(key, info, version) >= 0)
            ;
        return ver;
    }

    TaskID DistributedObjectStore::replicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff)
    {
        Version latest_version = replica_allocator_.getObjectVersion(key);
        if (latest_version < 0)
        {
            LOG(ERROR) << "cann't get version for key when replicating: " << key;
            return latest_version;
        }
        size_t existed_replica_number = replica_allocator_.getReplicaRealNumber(key, latest_version);
        if (existed_replica_number == 0)
        {
            LOG(ERROR) << "get existed_replica_number failed, no complete replica in this version, key: " << key << ", latest_version: " << latest_version;
            return getError(ERRNO::INVALID_VERSION);
        }

        // 比较新老config
        if (new_config.replica_num > existed_replica_number)
        { // need add
            for (size_t i = 0; i < new_config.replica_num - existed_replica_number; ++i)
            {
                bool success = false;
                uint32_t try_num = 0;
                std::vector<TransferRequest> transfer_tasks;
                ReplicaInfo existed_replica_info;
                ReplicaInfo new_replica_info;
                Version existed_version =
                    replica_allocator_.getOneReplica(key, existed_replica_info, latest_version, allocation_strategy_);
                if (existed_version < 0)
                {
                    LOG(ERROR) << "get existed replica failed in replicate operation, key: " << key << ", needed version: " << latest_version;
                    return existed_version;
                }
                Version add_version = replica_allocator_.addOneReplica(key, new_replica_info, existed_version, -1, allocation_strategy_);
                // replica_allocator_.addOneReplica(key, new_replica_info, latest_version, -1, allocation_strategy_);
                if (add_version < 0)
                {
                    LOG(ERROR) << "add replica failed in replicate operation, key: " << key << ", needed version: " << latest_version;
                    return add_version;
                }
                assert(existed_version == add_version);
                generateReplicaTransferRequests(existed_replica_info, new_replica_info, transfer_tasks);
                if (transfer_tasks.size() == 0)
                {
                    LOG(ERROR) << "no transfer tasks generated in replicate operation, key: " << key;
                    return getError(ERRNO::INVALID_REPLICA);
                }
                while (!success && try_num < max_trynum_)
                {
                    std::vector<TransferStatusEnum> status;
                    success = doReplica(transfer_tasks, status);
                    // success = doDummyReplica(transfer_tasks, status);
                    if (success)
                    { // update status
                        updateReplicaStatus(transfer_tasks, status, key, add_version, new_replica_info);
                        break;
                    }
                    ++try_num;
                }
            }
            replica_allocator_.cleanUncompleteReplica(key, latest_version, new_config.replica_num);
        }
        else if (new_config.replica_num < existed_replica_number)
        { // need remove
            for (size_t i = 0; i < existed_replica_number - new_config.replica_num; ++i)
            {
                ReplicaInfo replica_info;
                replica_allocator_.removeOneReplica(key, replica_info, latest_version);
            }
        }
        return latest_version;
    }

    void DistributedObjectStore::checkAll()
    {
        // 为不符合要求的重新分配空间
        replica_allocator_.checkall();
        std::unordered_map<ObjectKey, VersionList> &object_meta = replica_allocator_.getObjectMeta();
        // 遍历所有对象元数据
        for (auto &[key, version_list] : object_meta)
        {
            for (auto &[version, version_info] : version_list.versions)
            {
                // 检查是否有完整数据
                if (version_info.complete_replicas.empty())
                {
                    continue;
                }

                uint32_t complete_replica_id = *version_info.complete_replicas.begin();
                ReplicaInfo complete_replica_info = version_info.replicas[complete_replica_id];

                for (auto &[replica_id, replica_info] : version_info.replicas)
                {
                    if (replica_info.status == ReplicaStatus::PARTIAL)
                    {
                        std::vector<TransferRequest> transfer_tasks;
                        generateReplicaTransferRequests(complete_replica_info, replica_info, transfer_tasks);

                        bool success = false;
                        uint32_t try_num = 0;
                        while (!success && try_num < max_trynum_)
                        {
                            std::vector<TransferStatusEnum> status;
                            success = doReplica(transfer_tasks, status);
                            if (success)
                            {
                                updateReplicaStatus(transfer_tasks, status, key, version, replica_info);
                                break;
                            }
                            ++try_num;
                        }
                        if (!success)
                        {
                            LOG(ERROR) << "Failed to recover partial replica " << replica_id << " for key " << key << ", version " << version;
                        } // end if
                    } // end if PARIAL
                } // end for replicas
            } // end for versions
        } // end for object_meta
    }

    // private methods
    uint64_t DistributedObjectStore::calculateObjectSize(const std::vector<void *> &sizes)
    {
        // 实现计算对象大小的逻辑
        size_t total_size = 0;
        for (const auto &size : sizes)
        {
            total_size += reinterpret_cast<size_t>(size);
        }
        return total_size;
    }

    void DistributedObjectStore::generateWriteTransferRequests(
        const ReplicaInfo &replica_info,
        const std::vector<void *> &ptrs,
        const std::vector<void *> &sizes,
        std::vector<TransferRequest> &transfer_tasks)
    {
        // 实现生成写传输请求的逻辑
        size_t written = 0;
        size_t input_offset = 0;
        size_t input_idx = 0;

        // shard类型为 BufHandle
        for (const auto &handle : replica_info.handles)
        {
            size_t shard_offset = 0;

            while (shard_offset < handle->size && input_idx < ptrs.size())
            {
                size_t input_size = reinterpret_cast<size_t>(sizes[input_idx]);
                size_t remaining_input = input_size - input_offset;
                size_t remaining_shard = handle->size - shard_offset;
                size_t to_write = std::min(remaining_input, remaining_shard);

                // Copy data

                TransferRequest request;
                request.opcode = TransferRequest::OpCode::WRITE;
                request.source = (void *)(static_cast<char *>(ptrs[input_idx]) + input_offset);
                request.length = to_write;
                request.target_id = handle->segment_id;
                // TODO: 确认这里使用地址是否正确
                request.target_offset = (uint64_t)handle->buffer + shard_offset;
                transfer_tasks.push_back(std::move(request));

                LOG(INFO) << "create write request, input_idx: " << input_idx << ", input_offset: " << input_offset
                          << " , segmentid: " << handle->segment_id << ", shard_offset: " << shard_offset
                          << ", to_write_length: " << to_write << ", target offset:"
                          << (void *)request.target_offset << ", handle buffer: " << handle->buffer << std::endl;

                shard_offset += to_write;
                input_offset += to_write;
                written += to_write;

                if (input_offset == input_size)
                {
                    input_idx++;
                    input_offset = 0;
                }
            }

            LOG(INFO) << "Written " << shard_offset << " bytes to shard in node " << handle->segment_id << std::endl;
        }

        LOG(INFO) << "Total written for replica: " << written << " bytes" << std::endl;
        // 调用检查函数验证生成的请求是否符合预期
        if (!validateTransferRequests(replica_info, ptrs, sizes, transfer_tasks))
        {
            LOG(ERROR) << "Transfer requests validation failed!";
            // 可以根据需要抛出异常或处理错误
        }
        return;
    }

    void DistributedObjectStore::generateReadTransferRequests(
        const ReplicaInfo &replica_info,
        size_t offset,
        const std::vector<void *> &ptrs,
        const std::vector<void *> &sizes,
        std::vector<TransferRequest> &transfer_tasks)
    {
        // 实现生成读传输请求的逻辑
        size_t total_size = 0;
        for (const auto &size : sizes)
        {
            total_size += reinterpret_cast<size_t>(size);
        }
        LOG(INFO) << "generate read request, offset: " << offset << ", total_size: " << total_size << std::endl;
        size_t current_offset = 0;
        size_t remaining_offset = offset; // offset in input
        size_t bytes_read = 0;
        size_t output_index = 0;  // ptrs index
        size_t output_offset = 0; // offset in one ptr

        for (const auto &handle : replica_info.handles)
        {
            if (current_offset + handle->size <= offset)
            {
                current_offset += handle->size;
                remaining_offset -= handle->size;
                continue;
            }
            size_t shard_start = (remaining_offset > handle->size) ? 0 : remaining_offset;
            remaining_offset = (remaining_offset > handle->size) ? remaining_offset - handle->size : 0;

            TransferRequest request;
            while (shard_start < handle->size && bytes_read < total_size)
            {
                size_t bytes_to_read = std::min(
                    {handle->size - shard_start,
                     reinterpret_cast<size_t>(sizes[output_index]) - output_offset,
                     total_size - bytes_read});

                request.source = (void *)(static_cast<char *>(ptrs[output_index]) + output_offset);
                request.target_id = handle->segment_id;
                request.target_offset = (uint64_t)handle->buffer + shard_start; // TODO： 传入的是buffer地址，确认是否符合预期
                request.length = bytes_to_read;
                request.opcode = TransferRequest::OpCode::READ;
                transfer_tasks.push_back(std::move(request));

                LOG(INFO) << "read reqeust, source: " << request.source << ", target_id: " << request.target_id << ", target_offset: "
                          << (void *)request.target_offset << ", length: " << request.length << ", handle_size: " << handle->size << ", shard_start: " << shard_start
                          << ", output_size: " << sizes[output_index] << ", output_offset: " << output_offset
                          << " total_size: " << total_size << " ,bytes_read: " << bytes_read;

                shard_start += bytes_to_read;
                output_offset += bytes_to_read;
                bytes_read += bytes_to_read;

                if (output_offset == reinterpret_cast<size_t>(sizes[output_index]))
                {
                    output_index++;
                    output_offset = 0;
                }
            }

            current_offset += handle->size;
        }
        // 调用检查函数验证生成的请求是否符合预期
        if (!validateTransferReadRequests(replica_info, ptrs, sizes, transfer_tasks))
        {
            LOG(ERROR) << "Transfer requests validation failed!";
            // 可以根据需要抛出异常或处理错误
        }
        return;
    }

    void DistributedObjectStore::generateReplicaTransferRequests(
        const ReplicaInfo &existed_replica_info,
        const ReplicaInfo &new_replica_info,
        std::vector<TransferRequest> &transfer_tasks)
    {
        // 实现生成副本传输请求的逻辑
        std::vector<void *> ptrs;
        std::vector<void *> sizes;
        for (const auto &handle : existed_replica_info.handles)
        {
            ptrs.push_back(reinterpret_cast<void *>(handle->buffer));
            sizes.push_back(reinterpret_cast<void *>(handle->size));
        }
        // 复用 generateWriteTransferRequests 来生成传输请求
        generateWriteTransferRequests(new_replica_info, ptrs, sizes, transfer_tasks);
    }

    bool DistributedObjectStore::doWrite(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        // 实现写数据的逻辑
        // for (auto &task : transfer_tasks)
        // {
        //     void *target_address = (void *)task.target_offset;
        //     status.push_back(TransferStatusEnum::COMPLETED);
        //     std::memcpy(target_address, task.source, task.length);
        //     std::string str((char *)task.source, task.length);
        //     LOG(INFO) << "write data to " << (void *)target_address << " with size " << task.length;
        // }
        // return true;
        LOG(INFO) << "begin write data, task size: " << transfer_tasks.size();
        int ret = doTransfers(transfer_tasks, transfer_status);
        LOG(INFO) << "finish write data, task size: " << transfer_tasks.size();
        return (ret == 0) ? true : false;
    }

    bool DistributedObjectStore::doDummyWrite(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        // 实现写数据的逻辑
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

    bool DistributedObjectStore::doRead(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        LOG(INFO) << "begin read data, task size: " << transfer_tasks.size();
        int ret = doTransfers(transfer_tasks, transfer_status);
        LOG(INFO) << "finish read data, task size: " << transfer_tasks.size();
        return (ret == 0) ? true : false;
    }


    bool DistributedObjectStore::doDummyRead(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        transfer_status.clear();
        // 实现读数据的逻辑
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

    bool DistributedObjectStore::doReplica(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        // 实现执行副本操作的逻辑
        return doTransfers(transfer_tasks, transfer_status);
    }

    bool DistributedObjectStore::doDummyReplica(
        const std::vector<TransferRequest> &transfer_tasks,
        std::vector<TransferStatusEnum> &transfer_status)
    {
        // 实现执行副本操作的逻辑
        return doDummyWrite(transfer_tasks, transfer_status);
    }

    int DistributedObjectStore::doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status)
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
                return false;
        }
        ret = rdma_engine_->freeBatchID(batch_id);
        return ret;
    }

    bool DistributedObjectStore::validateTransferRequests(
        const ReplicaInfo &replica_info,
        const std::vector<void *> &ptrs,
        const std::vector<void *> &sizes, // 使用void*来表示大小
        const std::vector<TransferRequest> &transfer_tasks)
    {
        assert(ptrs.size() == sizes.size());
        if (transfer_tasks.size() == 0)
        {
            LOG(WARNING) << "tranfer task is 0 when in validateTransferRequests";
            return true;
        }
        // 将 sizes 转换为 uint64_t 类型
        std::vector<uint64_t> size_values;
        for (const auto &size_ptr : sizes)
        {
            size_values.push_back(reinterpret_cast<size_t>(size_ptr));
        }

        // 记录每个 segment_id 的累计写入量
        std::unordered_map<uint64_t, uint64_t> total_written_by_handle;
        size_t input_idx = 0;
        size_t input_offset = 0;

        // 遍历所有传输任务
        int handle_index = 0;
        uint64_t shard_offset = 0;
        for (size_t task_id = 0; task_id < transfer_tasks.size(); ++task_id)
        {
            const auto &task = transfer_tasks[task_id];
            LOG(INFO) << "the segment id: " << task.target_id << ", task length: " << task.length << " task target offset: " << (void *)task.target_offset << std::endl;
            // 找到对应的 BufHandle
            const auto &handle = replica_info.handles[handle_index];
            // 验证 source 地址是否正确
            if (reinterpret_cast<char *>(ptrs[input_idx]) + input_offset != task.source)
            {
                LOG(ERROR) << "Invalid source address. Expected: " << reinterpret_cast<char *>(ptrs[input_idx]) + input_offset
                           << ", Actual: " << task.source << std::endl;
                return false;
            }

            // 验证 length 是否正确
            if (size_values[input_idx] - input_offset < task.length)
            {
                google::FlushLogFiles(google::INFO);
                LOG(ERROR) << "Invalid length. Expected: " << size_values[input_idx] - input_offset
                           << ", Actual: " << task.length << std::endl;
                return false;
            }

            // 验证 target_offset 是否正确
            uint64_t expected_target_offset = (uint64_t)handle->buffer + shard_offset;
            if (expected_target_offset != task.target_offset)
            {
                LOG(ERROR) << "Invalid target_offset. Expected: " << (void *)expected_target_offset
                           << ", Actual: " << (void *)task.target_offset << std::endl;
                LOG(INFO) << "---------------------------------------------------";
                return false;
            }

            // 更新已写入字节数
            total_written_by_handle[reinterpret_cast<uint64_t>(handle->buffer)] += task.length;
            input_offset += task.length;
            shard_offset += task.length;
            LOG(INFO) << "task length: " << task.length << ", segment_id: " << handle->segment_id << ", shard_offset: " << shard_offset;

            // 如果当前数据块已全部写入，则移动到下一个数据块 || 到了最后一个任务了
            if (input_offset == size_values[input_idx] || task_id == transfer_tasks.size() - 1)
            {
                LOG(INFO) << "enter if: "
                          << "before_input_idx: " << input_idx << ", input_offset: " << input_offset << ", size_values[input_idx]: " << size_values[input_idx] << ", task_id: " << task_id << ", transfer_tasks.size(): " << transfer_tasks.size();
                input_idx++;
                input_offset = 0;
            }

            if (shard_offset >= handle->size)
            {
                handle_index++;
                shard_offset = 0;
            }

            LOG(INFO) << "Validated transfer task: "
                      << "input_idx: " << input_idx
                      << ", input_offset: " << input_offset
                      << ", segment_id: " << handle->segment_id
                      << std::hex
                      << ", target_offset: " << task.target_offset
                      << std::dec
                      << ", length: " << task.length << ", task_id: " << task_id;
            LOG(INFO) << "------------------------------------------------------------";
        }

        // 检查所有数据块是否都已处理
        if (size_values[ptrs.size() - 1] == 0)
        {
            // 由于最后一个元素大小为0，则tasks中不会包含,记录会少1 单独处理
            input_idx++;
        }
        if (input_idx != ptrs.size())
        {
            google::FlushLogFiles(google::INFO);
            LOG(ERROR) << "Not all input blocks were processed. Processed: " << input_idx << ", Total: " << ptrs.size() << std::endl;
            return false;
        }
        LOG(INFO) << "----------All transfer tasks validated successfully.----------------" << std::endl;
        return true;
    }

    bool DistributedObjectStore::validateTransferReadRequests(
        const ReplicaInfo &replica_info,
        const std::vector<void *> &ptrs,
        const std::vector<void *> &sizes,
        const std::vector<TransferRequest> &transfer_tasks)
    {
        size_t total_size = 0;
        for (const auto &size : sizes)
        {
            total_size += reinterpret_cast<size_t>(size);
        }

        size_t bytes_read = 0;
        size_t output_index = 0;
        size_t output_offset = 0;

        for (const auto &request : transfer_tasks)
        {
            // Check if the source address is within the range of ptrs
            bool valid_source = false;
            for (size_t i = 0; i < ptrs.size(); ++i)
            {
                if (request.source >= ptrs[i] &&
                    request.source < static_cast<char *>(ptrs[i]) + reinterpret_cast<size_t>(sizes[i]))
                {
                    valid_source = true;
                    break;
                }
            }
            if (!valid_source)
            {
                LOG(ERROR) << "Invalid source address in transfer request";
                return false;
            }

            // Check if the target offset is within the valid range
            bool valid_target = false;
            for (const auto &handle : replica_info.handles)
            {
                if (request.target_id == handle->segment_id &&
                    request.target_offset >= reinterpret_cast<uint64_t>(handle->buffer) &&
                    request.target_offset + request.length <= reinterpret_cast<uint64_t>(handle->buffer) + handle->size)
                {
                    valid_target = true;
                    break;
                }
            }
            if (!valid_target)
            {
                LOG(ERROR) << "Invalid target offset or length in transfer request";
                return false;
            }

            bytes_read += request.length;

            if (bytes_read > total_size)
            {
                LOG(ERROR) << "Total bytes read exceeds total size";
                return false;
            }

            output_offset += request.length;
            if (output_offset == reinterpret_cast<size_t>(sizes[output_index]))
            {
                output_index++;
                output_offset = 0;
            }
        }

        if (bytes_read > total_size)
        {
            LOG(ERROR) << "Total bytes read does not match total size, bytes_read: " << bytes_read << " ,total_read: " << total_size;
            return false;
        }
        else if (bytes_read < total_size)
        {
            LOG(WARNING) << "Total bytes read is less than total size, bytes_read: " << bytes_read << " ,total_read: " << total_size;
        }
        LOG(INFO) << "----------All transfer read tasks validated successfully.----------------" << std::endl;
        return true;
    }

} // namespace mooncake
