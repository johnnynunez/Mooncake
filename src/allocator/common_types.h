#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>

namespace mooncake {

using ObjectKey = std::string;
using Version = uint64_t;
using TaskID = uint64_t;



using SegmentID = int32_t;

struct ReplicaSource {
    SegmentID target_id = 0;
    size_t target_offset = 0;
    size_t length = 0;
};

struct TransferRequest
{
    enum OpCode
    {
        READ,
        WRITE,
        REPLICA_INCR,
        REPLICA_DECR,
        ILLEGAL,
    };

    OpCode opcode = ILLEGAL;                  // Default to ILLEGAL operation
    void *source = nullptr;                // Default to null pointer
    SegmentID target_id = 0;               // Default to some default SegmentID value
    size_t target_offset = 0;              // Default to 0 offset
    size_t length = 0;                     // Default to 0 length
    ReplicaSource source_replica = {};     // Default to default-constructed ReplicaSource
};



enum class PtrType
{
    HOST,
    DEVICE
};

enum class BufStatus
{
    INIT,
    PARTIAL,
    COMPLETE,
    OVERFLOW,
};

struct BufHandle
{

    int segment_id;
    uint64_t offset; // segment的整体偏移
    uint64_t based_offset;  // 一段buffer内的编译
    uint64_t size;
    BufStatus status;
};

struct ReplicateConfig
{
    int num_replicas;
    // 可以添加其他配置参数
};

enum class ReplicaStatus {
    INITIALIZED,
    DATA_LOADING,
    COMPLETED,
    FAILED
};

struct ReplicaInfo {
    std::vector<BufHandle> handles;
    ReplicaStatus status;
};

using ReplicaList = std::vector<ReplicaInfo>;

enum class ReplicaChangeStatus {
    ADDED,
    REMOVED,
    NO_CHANGE
};

struct ReplicaDiff {
    ReplicaList added_replicas;
    ReplicaList removed_replicas;
    ReplicaChangeStatus change_status;
};

struct VersionList
{
    std::map<Version, ReplicaList> versions; // 有序的
    uint64_t flushed_version;
    ReplicateConfig config;
};

}
