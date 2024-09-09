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


namespace mooncake {


// 上层 put get  replica recovery （给一个key，然后去遍历） --> 到这一层要怎么做
// 卸载时 如果handle有人用 
// 多线程  (加锁， 或者原子变量)


class ReplicaAllocator {
public:
    ReplicaAllocator(size_t shard_size);
    ~ReplicaAllocator();

    uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);

    Version addOneReplica(
        const ObjectKey& key,
        ReplicaInfo& ret,
        Version ver = -1,
        size_t object_size = -1,
        std::shared_ptr<AllocationStrategy> strategy = nullptr);
    
    Version getOneReplica(const ObjectKey& key, ReplicaInfo& ret, Version ver = -1, std::shared_ptr<AllocationStrategy> strategy = nullptr);
    
    void reassignReplica(const ObjectKey& key, Version ver, int replica_id, ReplicaInfo& ret);
    
    void removeOneReplica(const ObjectKey& key, ReplicaInfo& ret, Version ver = -1);
    
    // 卸载bufferallocator中包含的bufhandle,segment当前状态
    std::vector<std::shared_ptr<BufHandle>> unregister(SegmentId segment_id, uint64_t buffer_index);
    
    // 对old_handles中的BufHandle重新分配空间
    // return: 重新成功分配的个数
    size_t recovery(std::vector<std::shared_ptr<BufHandle>>& old_handles, std::shared_ptr<AllocationStrategy> strategy = nullptr);

    // 检查全部副本信息
    std::vector<std::shared_ptr<BufHandle>> checkall(); 

    // 获取全部object信息
    std::unordered_map<ObjectKey, VersionList>& getObjectMeta();

    // 获取shard大小
    
    size_t getShardSize();
    // 获取key对应的最新版本号
    Version getObjectVersion(ObjectKey key);

    // 获取key对应的副本config
    ReplicateConfig getObjectReplicaConfig(ObjectKey key);

    // 获取具体完整数据的副本个数
    size_t getReplicaRealNumber(ObjectKey key, Version version);

    // 清理没有完整数据的副本，并保留max_replica_num个完整的副本
    // 如果完整副本个数没有达到max_replica_num，则保留complete+partial 为max_replica_num个
    size_t cleanUncompleteReplica(ObjectKey key, Version version, int max_replica_num);

    // 更新副本状态, index指的是更新第几个副本，默认为最新的副本
    void updateStatus(const ObjectKey& key, ReplicaStatus status, size_t index = -1, Version ver = -1);

    // 对应key是否存在
    bool ifExist(const ObjectKey& key);

private:
    std::shared_ptr<BufHandle> allocateShard(SegmentId segment_id, int allocator_index, size_t size);

private:
    // 维护所有资源元信息
    std::map<SegmentId, std::vector<std::shared_ptr<BufferAllocator>>> buf_allocators_;
    std::map<SegmentId, std::map<uint64_t, std::vector<std::weak_ptr<BufHandle>>>> handles_;

    size_t shard_size_;
    std::atomic<uint64_t> global_version_;
    std::unordered_map<ObjectKey, VersionList> object_meta_;
    std::shared_ptr<AllocationStrategy> allocation_strategy_;

    int max_select_num_;

    // 添加读写锁保护元数据
    mutable std::shared_mutex object_meta_mutex_;
    mutable std::shared_mutex buf_allocators_mutex_;
};

}  // namespace mooncake