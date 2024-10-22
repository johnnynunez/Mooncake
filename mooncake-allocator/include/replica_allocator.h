// replica_allocator.h
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "allocation_strategy.h"
#include "buffer_allocator.h"

namespace mooncake
{

    class ReplicaAllocator
    {
    public:
        ReplicaAllocator(size_t shard_size);
        ~ReplicaAllocator();

        // Registers a buffer with the given segment ID, base offset, and size.
        // Returns the buffer index.
        uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);

        // Adds a new replica for the given object key.
        // Returns the version of the newly added replica.
        Version addOneReplica(
            const ObjectKey &key,
            ReplicaInfo &ret,
            Version ver = -1,
            uint64_t object_size = -1,
            std::shared_ptr<AllocationStrategy> strategy = nullptr);

        // Retrieves a replica for the given object key.
        // Returns the version of the retrieved replica.
        Version getOneReplica(
            const ObjectKey &key,
            ReplicaInfo &ret,
            Version ver = -1,
            std::shared_ptr<AllocationStrategy> strategy = nullptr);

        // Reassigns a replica for the given object key and version.
        void reassignReplica(
            const ObjectKey &key,
            Version ver,
            int replica_id,
            ReplicaInfo &ret);

        // Removes a replica for the given object key.
        // Returns the version of the removed replica.
        Version removeOneReplica(
            const ObjectKey &key,
            ReplicaInfo &ret,
            Version ver = -1);

        // Unregisters the buffer allocator for the given segment ID and buffer index.
        // Returns the list of buffer handles that were unregistered.
        std::vector<std::shared_ptr<BufHandle>> unregister(
            SegmentId segment_id,
            uint64_t buffer_index);

        // Recovers the given buffer handles by reallocating their space.
        // Returns the number of successfully reallocated handles.
        size_t recovery(
            std::vector<std::shared_ptr<BufHandle>> &old_handles,
            std::shared_ptr<AllocationStrategy> strategy = nullptr);

        // Checks all replicas and returns the list of buffer handles.
        std::vector<std::shared_ptr<BufHandle>> checkall();

        // Retrieves the metadata for all objects.
        std::unordered_map<ObjectKey, VersionList> &getObjectMeta();

        // Retrieves the shard size.
        size_t getShardSize();

        // Retrieves the latest version for the given object key.
        Version getObjectVersion(const ObjectKey &key);

        // Retrieves the replica configuration for the given object key.
        ReplicateConfig getObjectReplicaConfig(const ObjectKey &key);

        // Retrieves the number of real replicas for the given object key and version.
        size_t getReplicaRealNumber(const ObjectKey &key, Version version);

        // Cleans up incomplete replicas and ensures that at least `max_replica_num` complete replicas are retained.
        // If the number of complete replicas is less than `max_replica_num`, partial replicas are also retained.
        size_t cleanUncompleteReplica(
            const ObjectKey &key,
            Version version,
            int max_replica_num);

        // Updates the status of the replica for the given object key.
        // The index specifies which replica to update, defaulting to the latest replica.
        void updateStatus(
            const ObjectKey &key,
            ReplicaStatus status,
            size_t index = -1,
            Version ver = -1);

        // Gets the status of the replica for the given object key.
        std::vector<ReplicaStatus> getStatus(
            const ObjectKey &key,
            Version ver = -1);

        // Checks if the given object key exists.
        bool ifExist(const ObjectKey &key);

    private:
        // Allocates a shard for the given segment ID, buffer index, and size.
        std::shared_ptr<BufHandle> allocateShard(
            SegmentId segment_id,
            uint64_t allocator_index,
            size_t size);

    private:
        // Maintains metadata for all resources.
        std::map<SegmentId, std::vector<std::shared_ptr<BufferAllocator>>> buf_allocators_;
        std::map<SegmentId, std::map<uint64_t, std::vector<std::weak_ptr<BufHandle>>>> handles_;

        size_t shard_size_;
        std::atomic<uint64_t> global_version_;
        std::unordered_map<ObjectKey, VersionList> object_meta_;
        std::shared_ptr<AllocationStrategy> allocation_strategy_;

        int max_select_num_;

        // Adds read-write locks to protect metadata.
        mutable std::shared_mutex object_meta_mutex_;
        mutable std::shared_mutex buf_allocators_mutex_;
    };

} // namespace mooncake