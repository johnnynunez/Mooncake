// replica_allocator.cpp
#include "replica_allocator.h"
#include "random_allocation_strategy.h"
#include <glog/logging.h>
#include <stdexcept>

namespace mooncake
{

    ReplicaAllocator::ReplicaAllocator(size_t shard_size) : shard_size_(shard_size), global_version_(0)
    { // version从1开始
        auto config = std::make_shared<RandomAllocationStrategyConfig>();
        allocation_strategy_ = std::make_unique<RandomAllocationStrategy>(config);
        max_select_num_ = 30;
        LOG(INFO) << "ReplicaAllocator initialized with shard size: " << shard_size;
    }

    ReplicaAllocator::~ReplicaAllocator() = default;

    size_t ReplicaAllocator::getShardSize()
    {
        return shard_size_;
    }
    uint64_t ReplicaAllocator::registerBuffer(SegmentId segment_id, size_t base, size_t size)
    {
        std::unique_lock<std::shared_mutex> lock(buf_allocators_mutex_); // write lock
        buf_allocators_[segment_id].emplace_back(std::make_shared<BufferAllocator>(segment_id, base, size));

        LOG(INFO) << "Registered buffer for segment " << segment_id << " with base " << (void *)base << " and size " << size;
        return buf_allocators_[segment_id].size() - 1;
    }

    Version ReplicaAllocator::addOneReplica(
        const ObjectKey &key,
        ReplicaInfo &ret,
        Version ver,
        uint64_t object_size,
        std::shared_ptr<AllocationStrategy> strategy)
    {
        ret.reset();
        if (ver == DEFAULT_VALUE && object_size == DEFAULT_VALUE)
        {
            LOG(ERROR) << "Invalid arguments: ver and object_size cannot both be DEFAULT_VALUE";
            throw std::invalid_argument("ver and object_size cannot both be DEFAULT_VALUE");
        }

        std::unique_lock<std::shared_mutex> lock(object_meta_mutex_); // write lock
        if (object_meta_.count(key) == 0)
        { // Key does not exist
            ver = DEFAULT_VALUE;
        }
        auto &version_list = object_meta_[key];

        Version target_version = (ver != DEFAULT_VALUE) ? ver : ++global_version_;

        auto &version_info = version_list.versions[target_version];
        auto &replicas = version_info.replicas;
        uint64_t new_replica_id = version_info.max_replica_id.fetch_add(1);
        size_t size = 0;

        if (object_size != DEFAULT_VALUE && ver == DEFAULT_VALUE)
        {
            size = object_size;
        }
        else
        { // If both ver and object_size are specified, use ver information
            if (replicas.size() == 0)
            {
                LOG(ERROR) << "no completed replica for addonereplica, key: " << key << " ,version: " << ver;
                return getError(ERRNO::INVALID_VERSION);
            }
            for (const auto &handle : replicas.begin()->second.handles)
            {
                size += handle->size;
            }
        }

        if (!strategy)
        {
            strategy = allocation_strategy_;
        }

        size_t num_shards = (size + shard_size_ - 1) / shard_size_;
        ret.handles.reserve(num_shards);

        LOG(INFO) << "Adding replica for key " << key << ", version " << target_version << ", size " << size
                  << ", num_shards " << num_shards;

        // Allocate space for each shard
        for (size_t i = 0; i < num_shards; ++i)
        {
            size_t shard_size;
            if (i == num_shards - 1)
            {
                shard_size = size % shard_size_;
                if (shard_size == 0)
                {
                    shard_size = shard_size_; // If the last shard size is 0, set it to shard_size_
                }
            }
            else
            {
                shard_size = shard_size_;
            }
            int try_num = 0;
            bool allocated = false;
            while (!allocated)
            {
                std::shared_lock<std::shared_mutex> buf_lock(buf_allocators_mutex_);
                std::vector<SegmentId> failed_segment_ids;
                try_num++;
                if (try_num > max_select_num_)
                {
                    LOG(ERROR) << "Can't select good segment for addreplica after trying";
                    return getError(ERRNO::AVAILABLE_SEGMENT_EMPTY);
                }
                SegmentId segment_id = strategy->selectSegment(buf_allocators_, replicas, i, failed_segment_ids);
                if (buf_allocators_.count(segment_id) == 0)
                {
                    LOG(WARNING) << "Selected segment " << segment_id << " not found in buf_allocators_";
                    continue;
                }

                // Try to allocate space in the selected segment
                for (size_t j = 0; j < buf_allocators_[segment_id].size(); ++j)
                {
                    auto handle = allocateShard(segment_id, j, shard_size);
                    if (handle)
                    {
                        handle->replica_meta.object_name = key;
                        handle->replica_meta.version = target_version;
                        handle->replica_meta.replica_id = new_replica_id;
                        handle->replica_meta.shard_id = ret.handles.size();

                        ret.handles.push_back(handle);
                        handles_[segment_id][j].push_back(handle); // For global handle indexing
                        strategy->selected(segment_id, j, shard_size);
                        allocated = true;
                        LOG(INFO) << "Allocated shard " << i << " in segment " << segment_id << ", allocator " << j;
                        break;
                    }
                    else
                    {
                        LOG(WARNING) << "Failed to allocate shard " << i << " in segment " << segment_id << ", allocator "
                                     << j;
                    }
                }
            }
        }
        if (ret.handles.size() != num_shards)
        {
            LOG(ERROR) << "Cannot allocate needed handle, need : " << num_shards << " , allocate: " << ret.handles.size();
            return getError(ERRNO::BUFFER_OVERFLOW);
        }

        // Add new replica information
        ret.status = ReplicaStatus::INITIALIZED;
        uint32_t replica_id = new_replica_id;
        ret.replica_id = replica_id;
        replicas[replica_id] = ret;
        LOG(INFO) << "Added replica for key " << key << ", version " << target_version << ", replica_id: " << replica_id;
        return target_version;
    }

    Version ReplicaAllocator::getOneReplica(
        const ObjectKey &key,
        ReplicaInfo &ret,
        Version ver,
        std::shared_ptr<AllocationStrategy> strategy)
    {
        std::shared_lock<std::shared_mutex> lock(object_meta_mutex_); // read lock
        if (object_meta_.count(key) == 0)
        {
            LOG(WARNING) << "getOneReplica, the key " << key << " isnot existed";
            return getError(ERRNO::INVALID_KEY);
        }
        auto &version_list = object_meta_[key];
        Version target_version = version_list.flushed_version;
        if (target_version < ver)
        {
            LOG(ERROR) << "Invalid version " << ver << ", current version is " << target_version;
            ret.status = ReplicaStatus::FAILED;
            return getError(ERRNO::INVALID_VERSION);
        }
        if (!strategy)
        {
            strategy = allocation_strategy_;
        }
        if (version_list.versions.count(target_version) == 0 ||
            version_list.versions[target_version].replicas.size() == 0)
        {
            LOG(ERROR) << "No replica found for key " << key << ", version " << target_version;
            ret.status = ReplicaStatus::FAILED;
            return getError(ERRNO::INVALID_VERSION);
        }

        auto &version_info = version_list.versions[target_version];
        auto &replicas = version_info.replicas;
        std::vector<std::shared_ptr<BufHandle>> failed_bufhandle;

        size_t current_handle_index = 0;
        ret.status = ReplicaStatus::COMPLETE;
        size_t handles_per_replica = replicas.begin()->second.handles.size();
        for (size_t i = 0; i < handles_per_replica; ++i)
        {
            try
            {
                auto handle = strategy->selectHandle(replicas, current_handle_index, failed_bufhandle);
                if (handle == nullptr)
                {
                    throw std::runtime_error("Unable to select handle even after reset");
                }
                ret.handles.push_back(handle);
                ret.replica_id = handle->replica_meta.replica_id; // Temporarily take the replica_id of the first handle
                current_handle_index++;
            }
            catch (const std::exception &e)
            {
                LOG(ERROR) << "Failed to select handle: " << e.what();
                ret.status = ReplicaStatus::FAILED;
                return getError(ERRNO::NO_AVAILABLE_HANDLE);
            }
        }
        LOG(INFO) << "get one replica, key " << key << ", version " << target_version << ", replica_id " << ret.replica_id
                  << ", handles " << ret.handles.size() << ", status " << (int)ret.status;
        return target_version;
    }

    void ReplicaAllocator::reassignReplica(const ObjectKey &key, Version ver, int replica_id, ReplicaInfo &ret)
    {
        std::unique_lock<std::shared_mutex> lock(object_meta_mutex_); // write lock
        auto &version_list = object_meta_[key];
        auto &version_info = version_list.versions[ver];
        auto &old_replica = version_info.replicas[replica_id];

        LOG(INFO) << "Reassigning replica for key " << key << ", version " << ver << ", replica_id " << replica_id;

        // Reallocate or retain each shard
        for (size_t i = 0; i < old_replica.handles.size(); ++i)
        {
            if (old_replica.handles[i]->status == BufStatus::FAILED)
            {
                // Reallocate this shard
                size_t shard_size = old_replica.handles[i]->size;
                auto new_handle = allocateShard(DEFAULT_VALUE, DEFAULT_VALUE, shard_size);
                ret.handles.push_back(new_handle);
                LOG(INFO) << "Reallocated shard " << i << " with size " << shard_size;
            }
            else
            {
                ret.handles.push_back(old_replica.handles[i]);
                LOG(INFO) << "Retained shard " << i;
            }
        }
        // Update replica information
        ret.status = ReplicaStatus::INITIALIZED;
        version_info.replicas[replica_id] = ret;
        LOG(INFO) << "Completed reassignment of replica for key " << key << ", version " << ver << ", replica_id "
                  << replica_id;
    }

    Version ReplicaAllocator::removeOneReplica(const ObjectKey &key, ReplicaInfo &ret, Version ver)
    {
        std::unique_lock<std::shared_mutex> lock(object_meta_mutex_); // write lock
        auto &version_list = object_meta_[key];
        Version target_version = (ver != DEFAULT_VALUE) ? ver : version_list.flushed_version;

        auto &version_info = version_list.versions[target_version];
        auto &replicas = version_info.replicas;

        if (!replicas.empty())
        {
            auto first_replica_it = replicas.begin();
            ret = first_replica_it->second;

            if (ret.status == ReplicaStatus::COMPLETE)
            {
                version_info.complete_replicas.erase(first_replica_it->first);
            }
            ret.status = ReplicaStatus::REMOVED;
            replicas.erase(first_replica_it);

            LOG(INFO) << "Removed replica for key " << key << ", version " << target_version;
        }
        else
        {
            LOG(WARNING) << "No replicas to remove for key " << key << ", version " << target_version;
            return getError(ERRNO::INVALID_VERSION);
        }
        return target_version;
    }

    std::vector<std::shared_ptr<BufHandle>> ReplicaAllocator::unregister(SegmentId segment_id, uint64_t buffer_index)
    {
        std::unique_lock<std::shared_mutex> buf_lock(buf_allocators_mutex_); // write lock

        // Unregister the corresponding bufhandle in the replica and release the bufallocator
        std::vector<std::shared_ptr<BufHandle>> handles;
        if (buf_allocators_.count(segment_id) == 0 || buffer_index >= buf_allocators_[segment_id].size())
        {
            LOG(WARNING) << "Failed to unregister buffer for segment " << segment_id << ", buffer index " << buffer_index;
            return handles;
        }
        // Iterate through all handles in segment_id
        for (auto &handle : handles_[segment_id][buffer_index])
        {
            if (!handle.expired())
            {
                std::shared_ptr<BufHandle> h = handle.lock();
                h->status = BufStatus::UNREGISTERED;
                handles.push_back(h);
                LOG(INFO) << "need unregister handle"
                          << " , handle name: " << h->replica_meta.object_name
                          << " , replica id: " << h->replica_meta.replica_id
                          << " , shard index: " << h->replica_meta.shard_id;
            }
        }
        // Destroy the entire buf_allocators_[segment_id]
        buf_allocators_[segment_id].erase(buf_allocators_[segment_id].begin() + buffer_index);

        LOG(INFO) << "Unregistered buffer for segment " << segment_id << ", buffer index " << buffer_index;
        return handles;
    }

    size_t ReplicaAllocator::recovery(
        std::vector<std::shared_ptr<BufHandle>> &old_handles,
        std::shared_ptr<AllocationStrategy> strategy)
    {
        std::unique_lock<std::shared_mutex> lock(object_meta_mutex_); // write lock
        if (!strategy)
        {
            strategy = allocation_strategy_;
        }
        size_t new_handles_num = 0;
        for (auto &old_handle : old_handles)
        {
            const auto &replica_meta = old_handle->replica_meta;
            if (object_meta_.count(replica_meta.object_name) == 0 ||
                object_meta_[replica_meta.object_name].versions.count(replica_meta.version) == 0)
            {
                LOG(ERROR) << "Invalid object meta, object_name: " << replica_meta.object_name
                           << ", version: " << replica_meta.version;
                continue;
            }
            auto &version_info = object_meta_[replica_meta.object_name].versions[replica_meta.version];
            auto &handles = version_info.replicas[replica_meta.replica_id].handles;

            bool allocated = false;
            int try_num = 0;
            std::vector<SegmentId> failed_segment_ids;
            while (!allocated && try_num++ < max_select_num_)
            {
                SegmentId segment_id = strategy->selectSegment(
                    buf_allocators_, version_info.replicas, replica_meta.shard_id, failed_segment_ids);
                if (buf_allocators_.count(segment_id) == 0)
                {
                    LOG(WARNING) << "Selected segment " << segment_id << " not found in buf_allocators_";
                    continue;
                }

                for (size_t j = 0; j < buf_allocators_[segment_id].size(); ++j)
                {
                    auto new_handle = allocateShard(segment_id, j, old_handle->size);
                    if (new_handle)
                    {
                        new_handle->replica_meta = old_handle->replica_meta;
                        handles[new_handle->replica_meta.shard_id] = new_handle;
                        strategy->selected(segment_id, j, old_handle->size);
                        allocated = true;
                        new_handles_num++;
                        LOG(INFO) << "Allocated shard in segment " << segment_id << ", allocator " << j;
                        break;
                    }
                    else
                    {
                        LOG(WARNING) << "Allocate shard failed, segment_id: " << segment_id << ", index:" << j;
                    }
                }
            }
            if (!allocated)
            {
                LOG(ERROR) << "Failed to allocate shard for recovery after " << try_num << " attempts.";
            }
        }
        LOG(INFO) << "recovery handles num: " << old_handles.size() << ", new handles num: " << new_handles_num;
        return new_handles_num;
    }

    std::vector<std::shared_ptr<BufHandle>> ReplicaAllocator::checkall()
    {
        std::vector<std::shared_ptr<BufHandle>> handles;
        {
            std::shared_lock<std::shared_mutex> lock(object_meta_mutex_); // read lock
            for (auto &[key, versionlist] : object_meta_)
            {
                for (auto &[ver, versioninfo] : versionlist.versions)
                {
                    for (auto &[replica_id, replica_info] : versioninfo.replicas)
                    {
                        if (versioninfo.complete_replicas.count(replica_id) > 0)
                        {
                            continue;
                        }
                        for (auto &handle : replica_info.handles)
                        {
                            if (handle->status != BufStatus::COMPLETE && handle->status != BufStatus::INIT)
                            {
                                handles.push_back(handle);
                            }
                        }
                    }
                }
            }
        }
        LOG(INFO) << "Recovery handles, size: " << handles.size();
        recovery(handles);
        for (auto &[key, versionlist] : object_meta_)
        {
            for (auto &[ver, versioninfo] : versionlist.versions)
            {
                std::vector<ReplicaInfo> temp_replicalist;
                for (auto &[replica_id, replicainfo] : versioninfo.replicas)
                {
                    bool replica_complete = true;
                    for (auto &handle : replicainfo.handles)
                    {
                        if (handle->status != BufStatus::COMPLETE)
                        {
                            replica_complete = false;
                        }
                    }
                    if (replica_complete == true)
                    {
                        updateStatus(key, ReplicaStatus::COMPLETE, replica_id, ver);
                    }
                }
            }
        }
        return handles;
    }

    bool ReplicaAllocator::ifExist(const ObjectKey &key)
    {
        return object_meta_.count(key) == 0 ? false : true;
    }

    void ReplicaAllocator::updateStatus(const ObjectKey &key, ReplicaStatus status, size_t index, Version ver)
    {
        std::unique_lock<std::shared_mutex> lock(object_meta_mutex_); // write lock
        if (object_meta_.count(key) == 0)
        {
            LOG(WARNING) << "Update status for non-existing key: " << key;
            return;
        }
        auto &version_list = object_meta_[key];
        if (ver == DEFAULT_VALUE)
        {
            ver = version_list.flushed_version;
        }
        if (version_list.versions.count(ver) == 0)
        {
            LOG(WARNING) << "Update status for non-existing version: " << ver;
            return;
        }

        if (index == DEFAULT_VALUE)
        {
            index = version_list.versions[ver].replicas.size() - 1;
        }
        version_list.versions[ver].replicas[index].status = status;
        if (status == ReplicaStatus::COMPLETE)
        {
            if (ver > version_list.flushed_version)
            {
                version_list.flushed_version = ver;
            }
            version_list.versions[ver].complete_replicas.insert(index);
        }
        else
        {
            version_list.versions[ver].complete_replicas.erase(index);
        }
    }

    std::vector<ReplicaStatus> ReplicaAllocator::getStatus(const ObjectKey &key, Version ver)
    {
        std::vector<ReplicaStatus> status;
        if (object_meta_.count(key) == 0)
        {
            LOG(WARNING) << "Get status for non-existing key: " << key;
            return status;
        }
        auto &version_list = object_meta_[key];
        if (ver == DEFAULT_VALUE)
        {
            ver = version_list.flushed_version;
        }

        if (version_list.versions.count(ver) == 0)
        {
            LOG(WARNING) << "Get status for non-existing version: " << ver;
            return status;
        }
        size_t replica_num = version_list.versions[ver].replicas.size();

        for (size_t i = 0; i < replica_num; i++)
        {
            status.push_back(version_list.versions[ver].replicas[i].status);
        }
        return status;
    }

    std::shared_ptr<BufHandle> ReplicaAllocator::allocateShard(SegmentId segment_id, uint64_t allocator_index, size_t size)
    {
        std::shared_ptr<BufHandle> handle;
        if (segment_id == DEFAULT_VALUE || allocator_index == DEFAULT_VALUE)
        {
            std::shared_lock<std::shared_mutex> lock(buf_allocators_mutex_);
            // 选择一个可用的 allocator
            for (const auto &[sid, allocators] : buf_allocators_)
            {
                for (size_t i = 0; i < allocators.size(); ++i)
                {
                    if ((handle = allocators[i]->allocate(size)) != nullptr)
                    {
                        LOG(INFO) << "Allocated shard of size " << size << " in segment " << sid << ", allocator " << i;
                        return handle;
                    }
                }
            }
            LOG(ERROR) << "No available allocator found for shard of size " << size;
            return nullptr;
        }
        else
        {
            std::shared_lock<std::shared_mutex> lock(buf_allocators_mutex_);
            handle = buf_allocators_[segment_id][allocator_index]->allocate(size);

            if (handle)
            {
                LOG(INFO) << "Allocated shard of size " << size << " in segment " << segment_id << ", allocator "
                          << allocator_index;
            }
            else
            {
                LOG(WARNING) << "Failed to allocate shard of size " << size << " in segment " << segment_id
                             << ", allocator " << allocator_index;
            }
            return handle;
        }
    }

    std::unordered_map<ObjectKey, VersionList> &ReplicaAllocator::getObjectMeta()
    {
        return object_meta_;
    }

    Version ReplicaAllocator::getObjectVersion(const ObjectKey &key)
    {
        if (object_meta_.count(key) == 0)
        {
            return getError(ERRNO::INVALID_KEY);
        }
        return object_meta_[key].flushed_version;
    }

    ReplicateConfig ReplicaAllocator::getObjectReplicaConfig(const ObjectKey &key)
    {
        ReplicateConfig config;
        config.replica_num = 0;
        if (object_meta_.count(key) > 0)
        {
            config = object_meta_[key].config;
        }
        return config;
    }

    size_t ReplicaAllocator::getReplicaRealNumber(const ObjectKey &key, Version version)
    {
        if (object_meta_.count(key) == 0)
        {
            LOG(WARNING) << "no key: " << key << " existed, version:" << version << " when getReplicaRealNumber";
            return 0;
        }
        if (object_meta_[key].versions.count(version) == 0)
        {
            LOG(WARNING) << "key: " << key << " no existed version:" << version << " when getReplicaRealNumber";
            return 0;
        }
        return object_meta_[key].versions[version].complete_replicas.size();
    }

    size_t ReplicaAllocator::cleanUncompleteReplica(const ObjectKey &key, Version version, int max_replica_num)
    {
        auto &version_info = object_meta_[key].versions[version];
        int real_replica_num = version_info.complete_replicas.size();
        size_t cleanNum = 0;
        if (max_replica_num < real_replica_num)
        {
            LOG(WARNING) << "max_replica_num is over real_replica_num, shouldn't happen, max_replica_num: "
                         << max_replica_num << ", real_replica_num: " << real_replica_num;
            return 0;
        }

        int keep_partial_num = max_replica_num - real_replica_num;

        for (auto it = version_info.replicas.begin(); it != version_info.replicas.end();)
        {
            auto &replica = it->second;

            if (replica.status == ReplicaStatus::PARTIAL && keep_partial_num > 0)
            {
                keep_partial_num--;
                ++it;
            }
            else if (replica.status != ReplicaStatus::COMPLETE)
            {
                LOG(INFO) << "Removing partial replica: key: " << key << " version: " << version
                          << " replica_id: " << it->first;
                it = version_info.replicas.erase(it);
                cleanNum++;
            }
            else
            {
                LOG(INFO) << "Clean uncomplete replica, the key: " << key << " version: " << version
                          << " replica_id: " << it->first << " status: " << static_cast<uint64_t>(replica.status);
                ++it;
            }
        }
        return cleanNum;
    }

} // namespace mooncake