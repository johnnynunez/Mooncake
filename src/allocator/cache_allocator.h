#include <atomic>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <random>
#include <iostream>

#include "virtual_dummy_node.h"
#include "random_allocation_strategy.h"

namespace mooncake {

class CacheAllocator
{
private:
    std::atomic<uint64_t> global_version_;
    std::unordered_map<ObjectKey, VersionList> object_meta_;
    std::vector<std::unique_ptr<VirtualNode>> virtual_nodes_;
    std::unique_ptr<AllocationStrategy> allocation_strategy_;
    size_t shard_size_;

    ReplicaList allocateReplicas(size_t obj_size, int num_replicas);
    void writeDataToReplicas(ReplicaList &replicas, const std::vector<void *> &ptrs, const std::vector<void *> &sizes, int num_replicas);
    void updateObjectMeta(const ObjectKey &key, const ReplicaList &replicas, const ReplicateConfig &config);
    std::pair<Version, ReplicaList> getReplicas(const ObjectKey &key, Version version);
    size_t readAndCopyData(const std::vector<BufHandle> &replica,
                           size_t offset,
                           std::vector<void *> &ptrs,
                           const std::vector<void *> &sizes);

public:
    CacheAllocator(size_t shard_size, std::vector<std::unique_ptr<VirtualNode>> nodes, std::unique_ptr<AllocationStrategy> strategy);

    TaskID asyncPut(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, ReplicateConfig config);
    TaskID asyncReplicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff);
    TaskID asyncGet(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, Version min_version = 0, size_t offset = 0);
};

} // namespace mooncake
