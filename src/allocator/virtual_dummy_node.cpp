#include "virtual_dummy_node.h"
#include <iostream>

namespace mooncake {
    
VirtualDummyNode::VirtualDummyNode(int id) : node_id_(id), next_offset_(0) {}

BufHandle VirtualDummyNode::allocate(size_t size)
{
    BufHandle handle;
    handle.segment_id = node_id_;
    handle.offset = next_offset_;
    handle.size = size;
    handle.status = BufStatus::INIT;

    char *buffer = new char[size];
    buffers_[next_offset_] = buffer;

    next_offset_ += size;
    return handle;
}

void VirtualDummyNode::deallocate(const BufHandle &handle)
{
    auto it = buffers_.find(handle.offset);
    if (it != buffers_.end())
    {
        delete[] it->second;
        buffers_.erase(it);
        std::cout << "Deallocated buffer in node " << node_id_
                  << " at offset " << handle.offset
                  << " with size " << handle.size << std::endl;
    }
}

void *VirtualDummyNode::getBuffer(const BufHandle &handle)
{
    auto it = buffers_.find(handle.offset);
    return it != buffers_.end() ? it->second : nullptr;
}

void VirtualDummyNode::setExternalBuffer(const BufHandle &handle, char *buffer)
{
    auto it = buffers_.find(handle.offset);
    if (it != buffers_.end())
    {
        delete[] it->second;
    }
    buffers_[handle.offset] = buffer;
}

} // end namespace mooncake

