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
#define DEFAULT_VALUE UINT64_MAX
    using ObjectKey = std::string;
    using Version = uint64_t;
    using SegmentId = uint64_t;
    using TaskID = int64_t;

#define ERRNO_BASE DEFAULT_VALUE - 1000
    enum class ERRNO : uint64_t
    {
        // ok
        OK = ERRNO_BASE,
        // for buffer alloc
        BUFFER_OVERFLOW,          // 无法开辟合适的空间
                                  // for select segment
        SHARD_INDEX_OUT_OF_RANGE, // shard_index >= 副本handles个数
        AVAILABLE_SEGMENT_EMPTY,  //  available_segment为空
                                  // for select handle
        NO_AVAILABLE_HANDLE,
        // for version
        INVALID_VERSION,
        // for key
        INVALID_KEY,
        // for engine
        WRITE_FAIL,
        // for params
        INVALID_PARAMS,
        // for engine operation
        INVALID_WRITE,
        INVALID_READ,
        INVALID_REPLICA,
        // for transfer
        TRANSFER_FAIL,
    };

    const std::string &errnoToString(const int64_t errnoValue);

    const std::string &errEnumToString(const ERRNO errno);

    uint64_t getError(ERRNO err);

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

        uint64_t flushed_version = 0;
        ReplicateConfig config;
    };

    using BufHandleList = std::vector<std::shared_ptr<BufHandle>>;
    // using ReplicaList = std::vector<ReplicaInfo>;
    using ReplicaList = std::unordered_map<uint32_t, ReplicaInfo>;
    using BufferResources = std::map<SegmentId, std::vector<std::shared_ptr<BufferAllocator>>>;

    // define OperationStatus
    enum class OperationStatus : uint8_t
    {
        UNDEFINED = 0,
        PENDING,
        COMPLETE,
    };
#define TASK_STATUS std::pair<OperationStatus, ERRNO>
}