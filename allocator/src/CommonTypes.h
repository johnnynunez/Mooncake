#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>

using ObjectKey = std::string;
using Version = uint64_t;
using TaskID = uint64_t;

enum class PtrType
{
    HOST,
    DEVICE
};

enum class BufStatus
{
    INIT,
    PARTIAL,
    COMPLETE
};

struct BufHandle
{
    int segment_id;
    uint64_t offset;
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