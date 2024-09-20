#ifndef BUFFER_ALLOCATOR_H
#define BUFFER_ALLOCATOR_H

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "types.h"

#include "cachelib_memory_allocator/MemoryAllocator.h"

using facebook::cachelib::MemoryAllocator;
using facebook::cachelib::PoolId;

namespace mooncake
{

    // 多线程
    class BufferAllocator : public std::enable_shared_from_this<BufferAllocator>
    {
    public:
        BufferAllocator(int segment_id, size_t base, size_t size);

        ~BufferAllocator();

        std::shared_ptr<BufHandle> allocate(size_t size);

        void deallocate(BufHandle *handle);

    private:
        // metadata
        int segment_id_;
        size_t base_;
        size_t total_size_;

        // cachelib
        std::unique_ptr<char[]> header_region_start_;
        size_t header_region_size_;
        std::unique_ptr<facebook::cachelib::MemoryAllocator> memory_allocator_;
        facebook::cachelib::PoolId pool_id_;
    };

} // namespace mooncake

#endif // BUFFER_ALLOCATOR_H