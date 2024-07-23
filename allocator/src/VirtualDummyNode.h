#pragma once

#include "VirtualNode.h"
#include <unordered_map>

class VirtualDummyNode : public VirtualNode
{
private:
    int node_id;
    uint64_t next_offset;
    std::unordered_map<uint64_t, char *> buffers;

public:
    VirtualDummyNode(int id);
    BufHandle allocate(size_t size) override;
    void deallocate(const BufHandle &handle) override;
    void *getBuffer(const BufHandle &handle) override;
    void setExternalBuffer(const BufHandle &handle, char *buffer);
};