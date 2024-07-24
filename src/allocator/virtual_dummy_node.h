#pragma once

#include "virtual_node.h"
#include <unordered_map>

namespace mooncake {
    
class VirtualDummyNode : public VirtualNode
{
private:
    int node_id_;
    uint64_t next_offset_;
    std::unordered_map<uint64_t, char *> buffers_;

public:
    VirtualDummyNode(int id);
    BufHandle allocate(size_t size) override;
    void deallocate(const BufHandle &handle) override;
    void *getBuffer(const BufHandle &handle) override;
    void setExternalBuffer(const BufHandle &handle, char *buffer);
};


} // end namespace mooncake
