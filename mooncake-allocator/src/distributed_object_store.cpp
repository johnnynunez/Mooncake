#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include "config.h"
#include "distributed_object_store.h"
#include "dummy_transfer_agent.h"
#include "rdma_transfer_agent.h"
#include "transfer_agent.h"

namespace mooncake
{

    DistributedObjectStore::DistributedObjectStore()
        : replica_allocator_(std::stoi(ConfigManager::getInstance().get("shard_size", "65536"))),
          allocation_strategy_(nullptr),
          max_trynum_(std::stoi(ConfigManager::getInstance().get("max_trynum", "10")))
    {
        ConfigManager::getInstance().loadConfig("conf/allocator.conf");
        LOG(INFO) << "create the DistributedObjectStore!";

        // 初始化 TransferAgent
        transfer_agent_ = std::make_unique<RdmaTransferAgent>();
        transfer_agent_->init();
    }

    DistributedObjectStore::~DistributedObjectStore() {}

    void *DistributedObjectStore::allocateLocalMemory(size_t buffer_size)
    {
        return transfer_agent_->allocateLocalMemory(buffer_size);
    }

    SegmentId DistributedObjectStore::openSegment(const std::string &segment_name)
    {
        return transfer_agent_->openSegment(segment_name);
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

    void DistributedObjectStore::updateReplicaStatus(
        const std::vector<TransferRequest> &requests,
        const std::vector<TransferStatusEnum> &status,
        const std::string &key,
        const Version version,
        ReplicaInfo &replica_info)
    {
        bool ifCompleted = true;
        size_t shard_size = replica_allocator_.getShardSize();
        int handle_index = 0;
        uint64_t length = 0;
        std::unordered_set<int> failed_index;

        for (size_t i = 0; i < requests.size(); ++i)
        {
            LOG(INFO) << "Request index: " << i
                      << ", Handle index: " << handle_index;

            if (status[i] != TransferStatusEnum::COMPLETED)
            {
                replica_info.handles[handle_index]->status = BufStatus::FAILED;
                failed_index.insert(handle_index);
                replica_allocator_.updateStatus(key, ReplicaStatus::PARTIAL,
                                                replica_info.replica_id, version);
                LOG(WARNING) << "Handle " << i << " failed";
                ifCompleted = false;
            }
            else
            {
                // If any part of this handle failed, do not mark it as COMPLETE
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
            replica_allocator_.updateStatus(key, ReplicaStatus::COMPLETE,
                                            replica_info.replica_id, version);
            LOG(INFO) << "Key " << key
                      << ", Replica " << replica_info.replica_id
                      << " completed";
        }
    }

    TaskID DistributedObjectStore::put(
        ObjectKey key,
        const std::vector<Slice> &slices,
        ReplicateConfig config)
    {
        std::vector<TransferRequest> requests;
        int replica_num = config.replica_num;
        uint64_t total_size = 0;

        for (const auto &slice : slices)
        {
            total_size += slice.size;
        }

        if (total_size == 0)
        {
            LOG(WARNING) << "Size is 0";
            return getError(ERRNO::INVALID_PARAMS);
        }

        bool first_add = true;
        Version version = 0;

        // Regardless of whether the key exists, a new version will be created
        if (replica_allocator_.ifExist(key))
        {
            LOG(WARNING) << "Key already exists: " << key;
        }

        std::vector<ReplicaInfo> replica_infos(replica_num);
        std::vector<std::vector<TransferRequest>> all_requests(replica_num);

        // add all replicas
        for (int index = 0; index < replica_num; ++index)
        {
            if (first_add)
            {
                version = replica_allocator_.addOneReplica(key, replica_infos[index], DEFAULT_VALUE, total_size, allocation_strategy_);
            }
            else
            {
                version = replica_allocator_.addOneReplica(key, replica_infos[index], version, DEFAULT_VALUE, allocation_strategy_);
            }

            if (version > ERRNO_BASE)
            {
                LOG(ERROR) << "Failed to put object " << key
                           << ", size: " << total_size
                           << ", replica num: " << replica_num;
                break;
            }
            generateWriteTransferRequests(replica_infos[index], slices, all_requests[index]);
        }

        if (version > ERRNO_BASE)
        {
            // Cleanup if any replica addition failed
            for (int index = 0; index < replica_num; ++index)
            {
                ReplicaInfo ret;
                replica_allocator_.removeOneReplica(key, ret, version);
            }
            LOG(WARNING) << "No replica succeeded when putting, key: " << key
                         << ", replica_num: " << replica_num;
            return getError(ERRNO::WRITE_FAIL);
        }

        // Combine all requests into a single vector for batch processing
        std::vector<TransferRequest> combined_requests;
        for (const auto &requests : all_requests)
        {
            combined_requests.insert(combined_requests.end(), requests.begin(), requests.end());
        }

        // Perform batch transfer
        auto context = std::make_shared<TaskContext>();
        context->task_id = task_id_.fetch_add(1);
        context->key = key;
        context->version = version;
        context->replica_num = replica_num;
        context->replica_infos = std::move(replica_infos);
        context->all_requests = std::move(all_requests);
        context->task_size = combined_requests.size();

        auto batch_id = transfer_agent_->submitTransfersAsync(combined_requests);
        context->batch_id = batch_id;
        LOG(ERROR) << "the batchid : " << context->batch_id;
        if (batch_id == getError(ERRNO::TRANSFER_FAIL))
        {
            // 清理已分配的副本
            for (int index = 0; index < replica_num; ++index)
            {
                ReplicaInfo ret;
                replica_allocator_.removeOneReplica(key, ret, version);
            }
            LOG(WARNING) << "Failed to submit transfer requests, key: " << key;
            return getError(ERRNO::WRITE_FAIL);
        }
        // Store the context for later use
        task_contexts_[context->task_id] = context;

        LOG(INFO) << "Put object submitted asynchronously, key: " << key
                  << ", version: " << version
                  << ", replica_num: " << replica_num;
        return context->task_id;
    }

    void DistributedObjectStore::handlePutCompletion(
        std::shared_ptr<TaskContext> context,
        const std::vector<TransferStatusEnum> &status)
    {
        int succeed_num = 0;
        size_t status_index = 0;

        for (int index = 0; index < context->replica_num; ++index)
        {
            const auto &requests = context->all_requests[index];
            std::vector<TransferStatusEnum> replica_status(
                status.begin() + status_index,
                status.begin() + status_index + requests.size());
            updateReplicaStatus(requests, replica_status, context->key,
                                context->version, context->replica_infos[index]);
            status_index += requests.size();

            if (std::all_of(replica_status.begin(), replica_status.end(),
                            [](TransferStatusEnum s)
                            { return s == TransferStatusEnum::COMPLETED; }))
            {
                ++succeed_num;
            }
        }

        if (succeed_num == 0)
        {
            // 所有副本都失败，删除无用的副本
            for (int index = 0; index < context->replica_num; ++index)
            {
                ReplicaInfo ret;
                replica_allocator_.removeOneReplica(context->key, ret, context->version);
            }
            LOG(WARNING) << "No replica succeeded when putting, key: " << context->key
                         << ", replica_num: " << context->replica_num;
        }
        else
        {
            LOG(INFO) << "Put object completed asynchronously, key: " << context->key
                      << ", succeed num: " << succeed_num
                      << ", needed replica num: " << context->replica_num;
        }
    }
    TaskID DistributedObjectStore::get(
        ObjectKey key,
        std::vector<Slice> &slices,
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
        if (ver > ERRNO_BASE)
        {
            LOG(ERROR) << "Cannot get replica, key: " << key;
            return ver;
        }

        generateReadTransferRequests(replica_info, offset, slices, transfer_tasks);
        if (slices.empty() || transfer_tasks.empty())
        {
            LOG(ERROR) << "Slices or transfer tasks are empty";
            return getError(ERRNO::INVALID_READ);
        }

        while (!success && trynum < max_trynum_)
        {
            success = transfer_agent_->doTransfers(transfer_tasks, status);
            ++trynum;
            LOG(WARNING) << "Retrying, trynum: " << trynum
                         << ", key: " << key;
        }

        if (trynum == max_trynum_)
        {
            LOG(ERROR) << "Read data failed after max tries: "
                       << max_trynum_ << ", key: " << key;
            return getError(ERRNO::INVALID_READ);
        }

        return ver;
    }

    TaskID DistributedObjectStore::remove(ObjectKey key, Version version)
    {
        ReplicaInfo info;
        Version ver;

        if (!replica_allocator_.ifExist(key))
        {
            LOG(WARNING) << "Key does not exist: " << key;
            return getError(ERRNO::INVALID_KEY);
        }

        ver = replica_allocator_.removeOneReplica(key, info, version);
        while (replica_allocator_.removeOneReplica(key, info, version) <= ERRNO_BASE)
        {
            // Continue removing replicas
        }

        return ver;
    }

    TaskID DistributedObjectStore::replicate(
        ObjectKey key,
        ReplicateConfig new_config,
        ReplicaDiff &replica_diff)
    {
        Version latest_version = replica_allocator_.getObjectVersion(key);
        if (latest_version > ERRNO_BASE)
        {
            LOG(ERROR) << "Cannot get version for key when replicating: " << key;
            return latest_version;
        }

        size_t existed_replica_number = replica_allocator_.getReplicaRealNumber(key, latest_version);
        if (existed_replica_number == 0)
        {
            LOG(ERROR) << "Failed to get existed replica number, no complete replica in this version, key: "
                       << key << ", latest_version: " << latest_version;
            return getError(ERRNO::INVALID_VERSION);
        }

        // Compare new and old configurations
        if (new_config.replica_num > existed_replica_number)
        {
            size_t add_replica_num = new_config.replica_num - existed_replica_number;
            std::vector<std::vector<TransferRequest>> all_requests(add_replica_num);

            // Need to add replicas
            for (size_t i = 0; i < add_replica_num; ++i)
            {
                bool success = false;
                uint32_t try_num = 0;
                std::vector<TransferRequest> transfer_tasks;
                ReplicaInfo existed_replica_info;
                ReplicaInfo new_replica_info;

                Version existed_version = replica_allocator_.getOneReplica(key, existed_replica_info, latest_version, allocation_strategy_);
                if (existed_version > ERRNO_BASE)
                {
                    LOG(ERROR) << "Failed to get existed replica in replicate operation, key: "
                               << key << ", needed version: " << latest_version;
                    return existed_version;
                }

                Version add_version = replica_allocator_.addOneReplica(key, new_replica_info, existed_version, DEFAULT_VALUE, allocation_strategy_);
                if (add_version > ERRNO_BASE)
                {
                    LOG(ERROR) << "Failed to add replica in replicate operation, key: "
                               << key << ", needed version: " << latest_version;
                    return add_version;
                }

                assert(existed_version == add_version);
                generateReplicaTransferRequests(existed_replica_info, new_replica_info, transfer_tasks);
                if (transfer_tasks.empty())
                {
                    LOG(ERROR) << "No transfer tasks generated in replicate operation, key: " << key;
                    return getError(ERRNO::INVALID_REPLICA);
                }
                // all_requests[i] = std::move(transfer_tasks);

                while (!success && try_num < max_trynum_)
                {
                    // TODO: 异步化
                    std::vector<TransferStatusEnum> status;
                    success = transfer_agent_->doTransfers(transfer_tasks, status);
                    if (success)
                    {
                        // Update status
                        updateReplicaStatus(transfer_tasks, status, key, add_version, new_replica_info);
                        break;
                    }
                    ++try_num;
                }
            }
            replica_allocator_.cleanUncompleteReplica(key, latest_version, new_config.replica_num);
        }
        else if (new_config.replica_num < existed_replica_number)
        {
            // Need to remove replicas
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
        // Reallocate space for objects that do not meet the requirements
        replica_allocator_.checkall();
        std::unordered_map<ObjectKey, VersionList> &object_meta = replica_allocator_.getObjectMeta();

        // Traverse all object metadata
        for (auto &[key, version_list] : object_meta)
        {
            for (auto &[version, version_info] : version_list.versions)
            {
                // Check if there is complete data
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
                            success = transfer_agent_->doTransfers(transfer_tasks, status);
                            if (success)
                            {
                                updateReplicaStatus(transfer_tasks, status, key, version, replica_info);
                                break;
                            }
                            ++try_num;
                        }
                        if (!success)
                        {
                            LOG(ERROR) << "Failed to recover partial replica " << replica_id
                                       << " for key " << key << ", version " << version;
                        }
                    }
                }
            }
        }
    }

    TASK_STATUS DistributedObjectStore::getTaskStatus(TaskID task_id)
    {
        auto it = task_contexts_.find(task_id);
        if (it == task_contexts_.end())
        {
            LOG(ERROR) << "Task id not found: " << task_id;
            return std::make_pair<OperationStatus, ERRNO>(OperationStatus::COMPLETE, ERRNO::INVALID_KEY);
        }

        auto context = it->second;
        std::vector<TransferStatusEnum> status;

        // Update the put status and get the current transfer status
        return updatePutStatus(task_id, status);
    }

    // Private methods
    TASK_STATUS DistributedObjectStore::updatePutStatus(TaskID task_id, std::vector<TransferStatusEnum> &status)
    {
        auto it = task_contexts_.find(task_id);
        if (it == task_contexts_.end())
        {
            return std::make_pair<OperationStatus, ERRNO>(OperationStatus::COMPLETE, ERRNO::INVALID_KEY);
        }
        else if (it->second->task_status.first == OperationStatus::COMPLETE)
        {
            return it->second->task_status;
        }

        auto context = it->second;
        status.resize(it->second->task_size, TransferStatusEnum::PENDING);

        transfer_agent_->monitorTransferStatus(context->batch_id, context->task_size, status);

        // Check if all transfers are completed
        bool all_completed = std::all_of(status.begin(), status.end(),
                                         [](TransferStatusEnum s)
                                         { return s == TransferStatusEnum::COMPLETED || s == TransferStatusEnum::FAILED; });

        if (all_completed)
        {
            handlePutCompletion(context, status);
            // 判断在status中是否有TransferStatusEnum::FAILED, 如果有一个则ERRNO为TRANSFER_FAIL 并返回
            auto op_status = std::make_pair<OperationStatus, ERRNO>(OperationStatus::COMPLETE,
                                                                    std::any_of(status.begin(), status.end(),
                                                                                [](TransferStatusEnum s)
                                                                                { return s == TransferStatusEnum::FAILED; })
                                                                        ? ERRNO::TRANSFER_FAIL
                                                                        : ERRNO::OK);
            it->second->task_status = op_status;
            return op_status;
        }
        // 返回PENDING状态
        auto op_status = std::make_pair<OperationStatus, ERRNO>(OperationStatus::PENDING, ERRNO::OK);
        it->second->task_status = op_status;
        return op_status;
    }

    uint64_t DistributedObjectStore::calculateObjectSize(const std::vector<void *> &sizes)
    {
        size_t total_size = 0;
        for (const auto &size : sizes)
        {
            total_size += reinterpret_cast<size_t>(size);
        }
        return total_size;
    }

    void DistributedObjectStore::generateWriteTransferRequests(
        const ReplicaInfo &replica_info,
        const std::vector<Slice> &slices,
        std::vector<TransferRequest> &transfer_tasks)
    {
        size_t written = 0;
        size_t input_offset = 0;
        size_t input_idx = 0;

        for (const auto &handle : replica_info.handles)
        {
            size_t shard_offset = 0;

            while (shard_offset < handle->size && input_idx < slices.size())
            {
                const auto &current_slice = slices[input_idx];
                size_t remaining_input = current_slice.size - input_offset;
                size_t remaining_shard = handle->size - shard_offset;
                size_t to_write = std::min(remaining_input, remaining_shard);

                TransferRequest request;
                request.opcode = TransferRequest::OpCode::WRITE;
                request.source = static_cast<char *>(current_slice.ptr) + input_offset;
                request.length = to_write;
                request.target_id = handle->segment_id;
                request.target_offset = reinterpret_cast<uint64_t>(handle->buffer) + shard_offset;
                transfer_tasks.push_back(std::move(request));

                LOG(INFO) << "Create write request, input_idx: " << input_idx
                          << ", input_offset: " << input_offset
                          << ", segmentid: " << handle->segment_id
                          << ", shard_offset: " << shard_offset
                          << ", to_write_length: " << to_write
                          << ", target offset: " << reinterpret_cast<void *>(request.target_offset)
                          << ", handle buffer: " << handle->buffer;

                shard_offset += to_write;
                input_offset += to_write;
                written += to_write;

                if (input_offset == current_slice.size)
                {
                    input_idx++;
                    input_offset = 0;
                }
            }

            LOG(INFO) << "Written " << shard_offset << " bytes to shard in node "
                      << handle->segment_id;
        }

        LOG(INFO) << "Total written for replica: " << written << " bytes";

        if (!validateTransferRequests(replica_info, slices, transfer_tasks))
        {
            LOG(ERROR) << "Transfer requests validation failed!";
        }
    }

    void DistributedObjectStore::generateReadTransferRequests(
        const ReplicaInfo &replica_info,
        size_t offset,
        const std::vector<Slice> &slices,
        std::vector<TransferRequest> &transfer_tasks)
    {
        size_t total_size = 0;
        for (const auto &slice : slices)
        {
            total_size += slice.size;
        }
        LOG(INFO) << "Generate read request, offset: " << offset
                  << ", total_size: " << total_size;

        size_t current_offset = 0;
        size_t remaining_offset = offset; // offset in input
        size_t bytes_read = 0;
        size_t output_index = 0;  // slices index
        size_t output_offset = 0; // offset in one slice

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
                size_t bytes_to_read = std::min({handle->size - shard_start,
                                                 slices[output_index].size - output_offset,
                                                 total_size - bytes_read});

                request.source = static_cast<char *>(slices[output_index].ptr) + output_offset;
                request.target_id = handle->segment_id;
                request.target_offset = reinterpret_cast<uint64_t>(handle->buffer) + shard_start;
                request.length = bytes_to_read;
                request.opcode = TransferRequest::OpCode::READ;
                transfer_tasks.push_back(std::move(request));

                LOG(INFO) << "Read request, source: " << request.source
                          << ", target_id: " << request.target_id
                          << ", target_offset: " << reinterpret_cast<void *>(request.target_offset)
                          << ", length: " << request.length
                          << ", handle_size: " << handle->size
                          << ", shard_start: " << shard_start
                          << ", output_size: " << slices[output_index].size
                          << ", output_offset: " << output_offset
                          << ", total_size: " << total_size
                          << ", bytes_read: " << bytes_read;

                shard_start += bytes_to_read;
                output_offset += bytes_to_read;
                bytes_read += bytes_to_read;

                if (output_offset == slices[output_index].size)
                {
                    output_index++;
                    output_offset = 0;
                }
            }

            current_offset += handle->size;
        }

        if (!validateTransferReadRequests(replica_info, slices, transfer_tasks))
        {
            LOG(ERROR) << "Transfer requests validation failed!";
            // Handle error as needed
        }
    }
    void DistributedObjectStore::generateReplicaTransferRequests(
        const ReplicaInfo &existed_replica_info,
        const ReplicaInfo &new_replica_info,
        std::vector<TransferRequest> &transfer_tasks)
    {
        // Implement logic to generate replica transfer requests
        std::vector<Slice> slices;
        for (const auto &handle : existed_replica_info.handles)
        {
            slices.push_back(Slice{reinterpret_cast<void *>(handle->buffer), handle->size});
        }

        // Reuse generateWriteTransferRequests to generate transfer requests
        generateWriteTransferRequests(new_replica_info, slices, transfer_tasks);
    }

    bool DistributedObjectStore::validateTransferRequests(
        const ReplicaInfo &replica_info,
        const std::vector<Slice> &slices,
        const std::vector<TransferRequest> &transfer_tasks)
    {
        if (transfer_tasks.empty())
        {
            LOG(WARNING) << "Transfer task is empty when validating";
            return true;
        }

        // Record the cumulative write amount for each segment_id
        std::unordered_map<uint64_t, uint64_t> total_written_by_handle;
        size_t input_idx = 0;
        size_t input_offset = 0;

        // Traverse all transfer tasks
        int handle_index = 0;
        uint64_t shard_offset = 0;
        for (size_t task_id = 0; task_id < transfer_tasks.size(); ++task_id)
        {
            const auto &task = transfer_tasks[task_id];
            LOG(INFO) << "Segment id: " << task.target_id
                      << ", task length: " << task.length
                      << ", task target offset: " << reinterpret_cast<void *>(task.target_offset);

            // Find the corresponding BufHandle
            const auto &handle = replica_info.handles[handle_index];

            // Validate source address
            if (static_cast<char *>(slices[input_idx].ptr) + input_offset != task.source)
            {
                LOG(ERROR) << "Invalid source address. Expected: "
                           << static_cast<char *>(slices[input_idx].ptr) + input_offset
                           << ", Actual: " << task.source;
                return false;
            }

            // Validate length
            if (slices[input_idx].size - input_offset < task.length)
            {
                LOG(ERROR) << "Invalid length. Expected: "
                           << slices[input_idx].size - input_offset
                           << ", Actual: " << task.length;
                return false;
            }

            // Validate target_offset
            uint64_t expected_target_offset = reinterpret_cast<uint64_t>(handle->buffer) + shard_offset;
            if (expected_target_offset != task.target_offset)
            {
                LOG(ERROR) << "Invalid target_offset. Expected: "
                           << reinterpret_cast<void *>(expected_target_offset)
                           << ", Actual: " << reinterpret_cast<void *>(task.target_offset);
                return false;
            }

            // Update written bytes
            total_written_by_handle[reinterpret_cast<uint64_t>(handle->buffer)] += task.length;
            input_offset += task.length;
            shard_offset += task.length;
            LOG(INFO) << "Task length: " << task.length
                      << ", segment_id: " << handle->segment_id
                      << ", shard_offset: " << shard_offset;

            // Move to the next data block if current block is fully written
            if (input_offset == slices[input_idx].size || task_id == transfer_tasks.size() - 1)
            {
                LOG(INFO) << "Enter if: before_input_idx: " << input_idx
                          << ", input_offset: " << input_offset
                          << ", slices[input_idx].size: " << slices[input_idx].size
                          << ", task_id: " << task_id
                          << ", transfer_tasks.size(): " << transfer_tasks.size();
                input_idx++;
                input_offset = 0;
            }

            if (shard_offset >= handle->size)
            {
                handle_index++;
                shard_offset = 0;
            }

            LOG(INFO) << "Validated transfer task: input_idx: " << input_idx
                      << ", input_offset: " << input_offset
                      << ", segment_id: " << handle->segment_id
                      << ", target_offset: " << reinterpret_cast<void *>(task.target_offset)
                      << ", length: " << task.length
                      << ", task_id: " << task_id;
            LOG(INFO) << "------------------------------------------------------------";
        }

        // Check if all input blocks were processed
        if (slices.back().size == 0)
        {
            // Last element size is 0, tasks will not include it, handle separately
            input_idx++;
        }
        if (input_idx != slices.size())
        {
            LOG(ERROR) << "Not all input blocks were processed. Processed: "
                       << input_idx << ", Total: " << slices.size();
            return false;
        }
        LOG(INFO) << "----------All transfer tasks validated successfully.----------------";
        return true;
    }

    bool DistributedObjectStore::validateTransferReadRequests(
        const ReplicaInfo &replica_info,
        const std::vector<Slice> &slices,
        const std::vector<TransferRequest> &transfer_tasks)
    {
        size_t total_size = 0;
        for (const auto &slice : slices)
        {
            total_size += slice.size;
        }

        size_t bytes_read = 0;
        size_t output_index = 0;
        size_t output_offset = 0;

        for (const auto &request : transfer_tasks)
        {
            // Check if the source address is within the range of slices
            bool valid_source = false;
            for (size_t i = 0; i < slices.size(); ++i)
            {
                if (request.source >= slices[i].ptr &&
                    request.source < static_cast<char *>(slices[i].ptr) + slices[i].size)
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
            if (output_offset == slices[output_index].size)
            {
                output_index++;
                output_offset = 0;
            }
        }

        if (bytes_read > total_size)
        {
            LOG(ERROR) << "Total bytes read does not match total size, bytes_read: "
                       << bytes_read << ", total_read: " << total_size;
            return false;
        }
        else if (bytes_read < total_size)
        {
            LOG(WARNING) << "Total bytes read is less than total size, bytes_read: "
                         << bytes_read << ", total_read: " << total_size;
        }
        LOG(INFO) << "----------All transfer read tasks validated successfully.----------------";
        return true;
    }

} // namespace mooncake