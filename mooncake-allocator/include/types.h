#pragma once

#include <atomic>
#include <glog/logging.h>
#include <map>
#include <memory>
#include <set>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace mooncake
{
    #define WRONG_VERSION 0
    using ObjectKey = std::string;
    using Version = int64_t;
    using SegmentId = int64_t;
    using TaskID = int64_t;

    enum class ERRNO : int64_t
    {
        // for buffer alloc
        BUFFER_OVERFLOW = -1, // 无法开辟合适的空间
        // for select segment
        SHARD_INDEX_OUT_OF_RANGE = -2, // shard_index >= 副本handles个数
        AVAILABLE_SEGMENT_EMPTY = -3,  //  available_segment为空
        // for select handle
        NO_AVAILABLE_HANDLE = -4,
        // for version
        INVALID_VERSION = -5,
        // for key
        INVALID_KEY = -6,
        // for engine
        WRITE_FAIL = -7,
        // for params
        INVALID_PARAMS = -8,
        // for engine operation
        INVALID_WRITE = -9,
        INVALID_READ = -10,
        INVALID_REPLICA = -11,

    };

    const std::string& errnoToString(const int64_t errnoValue);

    const std::string& errEnumToString(const ERRNO errno);

    int64_t getError(ERRNO err);

    enum class BufStatus
    {
        INIT,         // 初始化状态
        COMPLETE,     // 完整状态（已经被使用）
        FAILED,       // 使用失败 （如果没有分配成功，上游应该将handle赋值为此状态）
        UNREGISTERED, // 这段空间元数据已被删除
    };

    class BufferAllocator;

    struct MetaForReplica
    {
        ObjectKey object_name;
        Version version;
        uint64_t replica_id;
        uint64_t shard_id;
    };

    class BufHandle
    {
    public:
        SegmentId segment_id;
        uint64_t size;
        BufStatus status;
        MetaForReplica replica_meta;
        void *buffer;

    public:
        BufHandle(std::shared_ptr<BufferAllocator> allocator, int segment_id, uint64_t size, void *buffer);
        ~BufHandle();

    private:
        std::shared_ptr<BufferAllocator> allocator_;
    };

    enum class ReplicaStatus
    {
        UNDEFINED = 0, // 未初始化
        INITIALIZED,   // 空间已分配 待写入
        PARTIAL,       // 只写入成功了部分
        COMPLETE,      // 写入完成 此副本可用
        REMOVED,       // 此副本已被移除
        FAILED,        // reassign可以用作判断
    };

    struct ReplicateConfig
    {
        size_t replica_num;
    };

    struct ReplicaInfo
    {
        std::vector<std::shared_ptr<BufHandle>> handles;
        ReplicaStatus status;
        uint32_t replica_id;

        void reset()
        {
            handles.clear();
            status = ReplicaStatus::UNDEFINED;
        }
    };

    struct VersionInfo
    {
        std::unordered_map<uint32_t, ReplicaInfo> replicas;
        std::set<uint32_t> complete_replicas;
        std::atomic<uint64_t> max_replica_id;

        // 初始化max_replica_id
        VersionInfo() : max_replica_id(0) {}
    };

    struct VersionList
    {
        // std::map<Version, std::vector<ReplicaInfo>> versions;
        // std::map<Version, std::set<uint32_t>> real_replica_index; // 一个Version有哪些replica是完整的
        std::map<Version, VersionInfo> versions;

        int64_t flushed_version = -1;
        ReplicateConfig config;
    };

    using BufHandleList = std::vector<std::shared_ptr<BufHandle>>;
    // using ReplicaList = std::vector<ReplicaInfo>;
    using ReplicaList = std::unordered_map<uint32_t, ReplicaInfo>;
    using BufferResources = std::map<SegmentId, std::vector<std::shared_ptr<BufferAllocator>>>;
}