#include "VirtualDummyNode.h"
#include <iostream>

VirtualDummyNode::VirtualDummyNode(int id) : node_id(id), next_offset(0) {}

BufHandle VirtualDummyNode::allocate(size_t size)
{
    BufHandle handle;
    handle.segment_id = node_id;
    handle.offset = next_offset;
    handle.size = size;
    handle.status = BufStatus::INIT;

    char *buffer = new char[size];
    buffers[next_offset] = buffer;

    next_offset += size;
    return handle;
}

void VirtualDummyNode::deallocate(const BufHandle &handle)
{
    auto it = buffers.find(handle.offset);
    if (it != buffers.end())
    {
        delete[] it->second;
        buffers.erase(it);
        std::cout << "Deallocated buffer in node " << node_id
                  << " at offset " << handle.offset
                  << " with size " << handle.size << std::endl;
    }
}

void *VirtualDummyNode::getBuffer(const BufHandle &handle)
{
    auto it = buffers.find(handle.offset);
    return it != buffers.end() ? it->second : nullptr;
}

void VirtualDummyNode::setExternalBuffer(const BufHandle &handle, char *buffer)
{
    auto it = buffers.find(handle.offset);
    if (it != buffers.end())
    {
        delete[] it->second;
    }
    buffers[handle.offset] = buffer;
}