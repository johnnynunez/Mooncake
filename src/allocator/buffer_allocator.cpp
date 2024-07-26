
#include "buffer_allocator.h"
#include "common_types.h"

#include <iostream>

namespace mooncake {

    
BufferAllocator::BufferAllocator(std::string type, int segment_id, size_t base, size_t size) : 
    type_(type),
    segment_id_(segment_id), base_(base), 
    total_size_(size), remaining_size_(size), next_offset_(0) {}

BufHandle BufferAllocator::allocate(size_t size)
{
    BufHandle handle;
    if (size > remaining_size_) {
        handle.status = BufStatus::OVERFLOW;
        return handle;
    }
    
    handle.segment_id = segment_id_;
    handle.offset = base_ + next_offset_; 
    handle.based_offset = next_offset_; 
    handle.size = size;
    handle.status = BufStatus::INIT;

    // 其实无需allocatoe开辟真实空间
    char *buffer = new char[size];
    buffers_[next_offset_] = buffer;

    next_offset_ += size;
    remaining_size_ -= size;
    return handle;
}

size_t BufferAllocator::getRemainingSize() const {
    return remaining_size_;
}

void BufferAllocator::deallocate(const BufHandle &handle)
{
    auto it = buffers_.find(handle.offset);
    if (it != buffers_.end())
    {
        delete[] it->second;
        buffers_.erase(it);
        std::cout << "Deallocated buffer in node " << segment_id_
                  << " at offset " << handle.offset
                  << " with size " << handle.size << std::endl;
    }
}

void *BufferAllocator::getBuffer(const BufHandle &handle)
{
    auto it = buffers_.find(handle.offset);
    return it != buffers_.end() ? it->second : nullptr;
}

void BufferAllocator::setExternalBuffer(const BufHandle &handle, char *buffer)
{
    auto it = buffers_.find(handle.offset);
    if (it != buffers_.end())
    {
        delete[] it->second;
    }
    buffers_[handle.offset] = buffer;
}

} // end namespace mooncake

