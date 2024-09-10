// buffer_allocator_test.cpp
#include "buffer_allocator.h"
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace mooncake
{

    class BufferAllocatorTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // 初始化 glog
            google::InitGoogleLogging("BufferAllocatorTest");
            FLAGS_logtostderr = 1; // 将日志输出到标准错误
        }

        void TearDown() override
        {
            // 清理 glog
            google::ShutdownGoogleLogging();
        }
    };

    TEST_F(BufferAllocatorTest, AllocateAndDeallocate)
    {
        const int segment_id = 1;
        const size_t base = 0x100000000;
        const size_t size = 1024 * 1024 * 12; // 12MB  must multi of 4MB

        auto allocator = std::make_shared<BufferAllocator>(segment_id, base, size);

        // 分配内存
        size_t alloc_size = 1024;
        auto bufHandle = allocator->allocate(alloc_size);
        ASSERT_NE(bufHandle, nullptr);
        EXPECT_EQ(bufHandle->segment_id, segment_id);
        EXPECT_EQ(bufHandle->size, alloc_size);
        EXPECT_EQ(bufHandle->status, BufStatus::INIT);
        EXPECT_NE(bufHandle->buffer, nullptr);

        // 释放内存
        bufHandle.reset();
    }

    TEST_F(BufferAllocatorTest, AllocateMultiple)
    {
        const int segment_id = 2;
        const size_t base = 0x200000000;
        const size_t size = 1024 * 1024 * 12; // 12MB  must multi of 4MB

        auto allocator = std::make_shared<BufferAllocator>(segment_id, base, size);

        // 分配多个内存块
        size_t alloc_size = 1024 * 1024;
        std::vector<std::shared_ptr<BufHandle>> handles;
        for (int i = 0; i < 8; ++i)
        { // 分配了12M， 但是第10M时会分配不出来
            auto bufHandle = allocator->allocate(alloc_size);
            ASSERT_NE(bufHandle, nullptr);
            handles.push_back(bufHandle);
        }
        // 释放所有内存块
        handles.clear();
        LOG(INFO) << "clean the handles in AllocateMultiple";
        // for (int i = 0; i < 12; ++i) {
        //     auto bufHandle = allocator->allocate(alloc_size);
        //     ASSERT_NE(bufHandle, nullptr);
        //     handles.push_back(bufHandle);
        // }
    }

    TEST_F(BufferAllocatorTest, AllocateTooLarge)
    {
        const int segment_id = 3;
        const size_t base = 0x300000000;
        const size_t size = 1024 * 1024 * 12; // 12MB  must multi of 4MB

        auto allocator = std::make_shared<BufferAllocator>(segment_id, base, size);

        // 尝试分配超过总大小的内存
        size_t alloc_size = size + 1;
        auto bufHandle = allocator->allocate(alloc_size);
        EXPECT_EQ(bufHandle, nullptr);
    }

} // namespace mooncake

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
