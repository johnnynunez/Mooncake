// buffer_allocator.cpp
#include "buffer_allocator.h"
// 新添加的头文件
#include <glog/logging.h>

namespace mooncake
{

    BufHandle::BufHandle(std::shared_ptr<BufferAllocator> allocator, int segment_id, uint64_t size, void *buffer) : segment_id(segment_id), size(size), status(BufStatus::INIT), buffer(buffer), allocator_(allocator)
    {
        LOG(INFO) << "Created BufHandle for segment " << segment_id << ", size " << size << ", buffer address: " << buffer;
    }

    BufHandle::~BufHandle()
    {
        if (allocator_)
        {
            allocator_->deallocate(this);

            LOG(INFO) << "Deallocated BufHandle for segment " << segment_id << ", size " << size;
        }
        else
        {
            LOG(WARNING) << "allocator is nullptr when ~BufHandle";
        }
    }

    BufferAllocator::BufferAllocator(int segment_id, size_t base, size_t size) : segment_id_(segment_id), base_(base), total_size_(size)
    {
        LOG(INFO) << "Initializing BufferAllocator for segment " << segment_id << ", base " << (void *)base << ", size "
                  << size;

        // 计算头部区域的大小
        header_region_size_ =
            sizeof(facebook::cachelib::SlabHeader) * static_cast<unsigned int>(size / sizeof(facebook::cachelib::Slab)) + 1;
        header_region_start_ = std::make_unique<char[]>(header_region_size_);

        // 初始化 CacheLib MemoryAllocator
        memory_allocator_ = std::make_unique<facebook::cachelib::MemoryAllocator>(
            facebook::cachelib::MemoryAllocator::Config(facebook::cachelib::MemoryAllocator::generateAllocSizes()),
            reinterpret_cast<void *>(header_region_start_.get()),
            header_region_size_,
            reinterpret_cast<void *>(base),
            size);

        // 添加主池到分配器
        pool_id_ = memory_allocator_->addPool("main", size);

        LOG(INFO) << "BufferAllocator initialized with pool_id " << (int)pool_id_;
    }

    BufferAllocator::~BufferAllocator()
    {
        LOG(INFO) << "BufferAllocator destroyed";
    }

    std::shared_ptr<BufHandle> BufferAllocator::allocate(size_t size)
    {
        void *buffer = nullptr;
        try
        {
            // 使用 CacheLib 分配内存
            buffer = memory_allocator_->allocate(pool_id_, size);
            if (!buffer)
            {
                LOG(WARNING) << "Failed to allocate " << size << " bytes in segment " << segment_id_;
                return nullptr;
            }
        }
        catch (const std::exception &e)
        {
            // 捕获所有标准异常并打印异常信息
            LOG(ERROR) << "Exception caught during allocation: " << e.what();
            return nullptr;
        }
        catch (...)
        {
            // 捕获所有其他类型的异常
            LOG(ERROR) << "Unknown exception caught during allocation";
            return nullptr;
        }

        LOG(INFO) << "Allocated " << size << " bytes in segment " << segment_id_ << ", the address: " << buffer;
        // 创建并返回新的 BufHandle
        auto bufHandle = std::make_shared<BufHandle>(shared_from_this(), segment_id_, size, buffer);
        return bufHandle;
    }
    void BufferAllocator::deallocate(BufHandle *handle)
    {
        try
        {
            // 使用 CacheLib 释放内存
            memory_allocator_->free(handle->buffer);
            handle->status = BufStatus::UNREGISTERED;
            LOG(INFO) << "Deallocated address: " << handle->buffer << " , " << handle->size << " bytes in segment "
                      << segment_id_;
        }
        catch (const std::exception &e)
        {
            // 捕获所有标准异常并打印异常信息
            LOG(ERROR) << "Exception caught during deallocation: " << e.what();
        }
        catch (...)
        {
            // 捕获所有其他类型的异常
            LOG(ERROR) << "Unknown exception caught during deallocation";
        }
    }
} // namespace mooncake