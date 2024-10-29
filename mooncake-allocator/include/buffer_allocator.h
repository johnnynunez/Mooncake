// Copyright 2024 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BUFFER_ALLOCATOR_H
#define BUFFER_ALLOCATOR_H

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "cachelib_memory_allocator/MemoryAllocator.h"
#include "types.h"

using facebook::cachelib::MemoryAllocator;
using facebook::cachelib::PoolId;

namespace mooncake {

// 多线程
class BufferAllocator : public std::enable_shared_from_this<BufferAllocator> {
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

}  // namespace mooncake

#endif  // BUFFER_ALLOCATOR_H