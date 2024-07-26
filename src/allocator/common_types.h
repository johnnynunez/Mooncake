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
    SegmentID target_id;
    size_t target_offset;
    size_t length;
};

struct TransferRequest
{
    enum OpCode
    {
        READ,
        WRITE,
        REPLICA_INCR,
        REPLICA_DECR,
    };

    OpCode opcode;
    void *source;
    SegmentID target_id;
    size_t target_offset;
    size_t length;
    ReplicaSource source_replica;
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
