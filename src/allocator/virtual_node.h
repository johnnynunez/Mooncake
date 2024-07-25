#pragma once

#include "common_types.h"

class VirtualNode
{
public:
    virtual BufHandle allocate(size_t size) = 0;
    virtual void deallocate(const BufHandle &handle) = 0;
    virtual void *getBuffer(const BufHandle &handle) = 0;
    virtual ~VirtualNode() = default;
};