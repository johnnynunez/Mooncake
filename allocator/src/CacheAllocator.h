#include <atomic>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <random>
#include <iostream>

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

class VirtualNode
{
public:
    virtual BufHandle allocate(size_t size) = 0;
    virtual void deallocate(const BufHandle &handle) = 0;
    virtual void *getBuffer(const BufHandle &handle) = 0;
};

class VirtualDummyNode : public VirtualNode
{
private:
    int node_id;
    uint64_t next_offset;
    std::unordered_map<uint64_t, char *> buffers;

public:
    VirtualDummyNode(int id) : node_id(id), next_offset(0) {}

    BufHandle allocate(size_t size) override
    {
        BufHandle handle;
        handle.segment_id = node_id;
        handle.offset = next_offset;
        handle.size = size;
        handle.status = BufStatus::INIT;

        // 分配实际的缓冲区
        char *buffer = new char[size];
        buffers[next_offset] = buffer;

        next_offset += size;
        return handle;
    }

    void deallocate(const BufHandle &handle) override
    {
        // In a real implementation, we would manage the free space here
        // For this dummy version, we'll just print a message
        auto it = buffers.find(handle.offset);
        if (it != buffers.end())
        {
            delete[] it->second; // 释放内存
            buffers.erase(it);
            std::cout << "Deallocated buffer in node " << node_id
                      << " at offset " << handle.offset
                      << " with size " << handle.size << std::endl;
        }
        std::cout << "Deallocated buffer in node " << node_id
                  << " at offset " << handle.offset
                  << " with size " << handle.size << std::endl;
    }

    void *getBuffer(const BufHandle &handle) override
    {
        auto it = buffers.find(handle.offset);
        if (it != buffers.end())
        {
            return it->second;
        }
        return nullptr;
    }

    // TODO: 允许外部设置缓冲区
    void setExternalBuffer(const BufHandle &handle, char *buffer)
    {
        auto it = buffers.find(handle.offset);
        if (it != buffers.end())
        {
            delete[] it->second; // 释放旧的缓冲区
        }
        buffers[handle.offset] = buffer;
    }
};

class AllocationStrategy
{
public:
    virtual std::vector<int> selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) = 0;
    virtual ~AllocationStrategy() = default;
};

class RandomAllocationStrategy : public AllocationStrategy
{
private:
    std::mt19937 rng;

public:
    RandomAllocationStrategy()
    {
        std::random_device rd;
        rng = std::mt19937(rd());
    }

    std::vector<int> selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) override
    {
        int num_virtual_nodes = nodes.size();
        if (num_shards * num_replicas > num_virtual_nodes)
        {
            std::cout << "num_shards: " << num_shards << ", num_replicas: " << num_replicas << ", num_virtual_nodes: " << num_virtual_nodes << std::endl;
            throw std::runtime_error("Not enough virtual nodes to allocate all shards and replicas");
        }

        std::vector<int> all_nodes(num_virtual_nodes);
        for (int i = 0; i < num_virtual_nodes; ++i)
        {
            all_nodes[i] = i;
        }

        std::vector<int> selected_nodes;
        selected_nodes.reserve(num_shards * num_replicas);

        for (int replica = 0; replica < num_replicas; ++replica)
        {
            std::vector<int> available_nodes = all_nodes;

            for (int shard = 0; shard < num_shards; ++shard)
            {
                if (available_nodes.empty())
                {
                    available_nodes = all_nodes;
                }

                std::uniform_int_distribution<> dis(0, available_nodes.size() - 1);
                int index = dis(rng);
                int selected_node = available_nodes[index];

                selected_nodes.push_back(selected_node);

                available_nodes.erase(available_nodes.begin() + index);
            }
        }

        return selected_nodes;
    }
};

class CacheAllocator
{
private:
    std::atomic<uint64_t> global_version;
    std::unordered_map<ObjectKey, VersionList> object_meta;
    std::vector<std::unique_ptr<VirtualNode>> virtual_nodes;
    std::unique_ptr<AllocationStrategy> allocation_strategy;
    size_t shard_size;

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

    TaskID AsyncPut(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, ReplicateConfig config);
    TaskID AsyncReplicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff& replica_diff);
    TaskID AsyncGet(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, Version min_version = 0, size_t offset = 0);
};