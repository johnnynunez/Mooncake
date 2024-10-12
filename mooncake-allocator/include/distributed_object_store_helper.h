#include "distributed_object_store.h"

using namespace mooncake;

class PyDistributedObjectStore: public DistributedObjectStore {
    public:
        using DistributedObjectStore::DistributedObjectStore;

        uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);

        void unregisterBuffer(SegmentId segment_id, uint64_t index);

        TaskID put(ObjectKey key, std::vector<void *> ptrs, std::vector<void *> sizes, ReplicateConfig config);

        TaskID get(ObjectKey key, std::vector<void *> ptrs, std::vector<void *> sizes, Version min_version, size_t offset);

        TaskID remove(ObjectKey key, Version version = -1);

        TaskID replicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff);

        void checkAll();
};