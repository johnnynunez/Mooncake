#pragma once

#include <unordered_map>
#include <string>
#include <map>

#include "common_types.h"
#include "cachelib_memory_allocator/MemoryAllocator.h"

using facebook::cachelib::MemoryAllocator;
using facebook::cachelib::PoolId;

namespace mooncake {

class BufferAllocator
{
private:
    // CacheLib memory allocator 相关参数
    size_t header_region_size_;
    std::unique_ptr<char[]> header_region_start_;
    std::shared_ptr<MemoryAllocator> memory_allocator_;
    PoolId pool_id_;
    std::string type_;
    int node_id_;
    int segment_id_;
    uint64_t next_offset_;
    size_t total_size_;
    size_t remaining_size_;    // 剩余size
    size_t base_; 
    std::unordered_map<uint64_t, char *> buffers_;

public:
    BufferAllocator(std::string type, int id, size_t base, size_t size, void *memory_start);
    BufHandle allocate(size_t size);
    void deallocate(const BufHandle &handle);
    void *getBuffer(const BufHandle &handle);
    size_t getRemainingSize() const;
    void setExternalBuffer(const BufHandle &handle, char *buffer);
};

using BufferResources = std::map<std::string, std::map<int, std::vector<BufferAllocator>> >;


} // end namespace mooncake
