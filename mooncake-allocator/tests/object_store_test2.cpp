#include "distributed_object_store.h"
#include <gtest/gtest.h>

namespace mooncake
{

    class DistributedObjectStoreGenerateTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // 初始化 DistributedObjectStore 实例
            store = std::make_unique<DistributedObjectStore>();
        }

        void TearDown() override
        {
            // 清理资源
            store.reset();
        }

        std::unique_ptr<DistributedObjectStore> store;
    };

    TEST_F(DistributedObjectStoreGenerateTest, GenerateWriteTransferRequests)
    {
        ReplicaInfo replica_info;
        std::vector<void *> ptrs = {malloc(1024), malloc(1024)};
        std::vector<void *> sizes = {reinterpret_cast<void *>(512), reinterpret_cast<void *>(1024)};
        std::vector<TransferRequest> transfer_tasks;

        // 创建一些模拟的 BufHandle
        std::shared_ptr<BufHandle> handle1 = std::make_shared<BufHandle>(nullptr, 1, 512, malloc(512));
        std::shared_ptr<BufHandle> handle2 = std::make_shared<BufHandle>(nullptr, 2, 1024, malloc(1024));
        replica_info.handles = {handle1, handle2};

        store->generateWriteTransferRequests(replica_info, ptrs, sizes, transfer_tasks);

        // 验证生成的 transfer_tasks 是否正确
        ASSERT_EQ(transfer_tasks.size(), 3);

        // 验证第一个请求
        ASSERT_EQ(transfer_tasks[0].opcode, TransferRequest::OpCode::WRITE);
        ASSERT_EQ(transfer_tasks[0].source, ptrs[0]);
        ASSERT_EQ(transfer_tasks[0].target_id, 1);
        ASSERT_EQ(transfer_tasks[0].target_offset, 0);
        ASSERT_EQ(transfer_tasks[0].length, 512);

        // 验证第二个请求
        ASSERT_EQ(transfer_tasks[1].opcode, TransferRequest::OpCode::WRITE);
        ASSERT_EQ(transfer_tasks[1].source, ptrs[1]);
        ASSERT_EQ(transfer_tasks[1].target_id, 2);
        ASSERT_EQ(transfer_tasks[1].target_offset, 0);
        ASSERT_EQ(transfer_tasks[1].length, 512);

        // 验证第三个请求
        ASSERT_EQ(transfer_tasks[2].opcode, TransferRequest::OpCode::WRITE);
        ASSERT_EQ(transfer_tasks[2].source, static_cast<char *>(ptrs[1]) + 512);
        ASSERT_EQ(transfer_tasks[2].target_id, 2);
        ASSERT_EQ(transfer_tasks[2].target_offset, 512);
        ASSERT_EQ(transfer_tasks[2].length, 512);

        // 清理资源
        for (auto ptr : ptrs)
        {
            free(ptr);
        }
        free(handle1->buffer);
        free(handle2->buffer);
    }

    TEST_F(DistributedObjectStoreGenerateTest, GenerateReadTransferRequests)
    {
        std::vector<std::shared_ptr<BufHandle>> handles;
        std::vector<void *> ptrs = {malloc(1024), malloc(1024)};
        std::vector<void *> sizes = {reinterpret_cast<void *>(512), reinterpret_cast<void *>(1024)};
        std::vector<TransferRequest> transfer_tasks;

        // 创建一些模拟的 BufHandle
        std::shared_ptr<BufHandle> handle1 = std::make_shared<BufHandle>(nullptr, 1, 512, malloc(512));
        std::shared_ptr<BufHandle> handle2 = std::make_shared<BufHandle>(nullptr, 2, 1024, malloc(1024));
        handles = {handle1, handle2};

        store->generateReadTransferRequests(handles, 0, ptrs, sizes, transfer_tasks);

        // 验证生成的 transfer_tasks 是否正确
        ASSERT_EQ(transfer_tasks.size(), 3);

        // 验证第一个请求
        ASSERT_EQ(transfer_tasks[0].opcode, TransferRequest::OpCode::READ);
        ASSERT_EQ(transfer_tasks[0].source, ptrs[0]);
        ASSERT_EQ(transfer_tasks[0].target_id, 1);
        ASSERT_EQ(transfer_tasks[0].target_offset, 0);
        ASSERT_EQ(transfer_tasks[0].length, 512);

        // 验证第二个请求
        ASSERT_EQ(transfer_tasks[1].opcode, TransferRequest::OpCode::READ);
        ASSERT_EQ(transfer_tasks[1].source, ptrs[1]);
        ASSERT_EQ(transfer_tasks[1].target_id, 2);
        ASSERT_EQ(transfer_tasks[1].target_offset, 0);
        ASSERT_EQ(transfer_tasks[1].length, 512);

        // 验证第三个请求
        ASSERT_EQ(transfer_tasks[2].opcode, TransferRequest::OpCode::READ);
        ASSERT_EQ(transfer_tasks[2].source, static_cast<char *>(ptrs[1]) + 512);
        ASSERT_EQ(transfer_tasks[2].target_id, 2);
        ASSERT_EQ(transfer_tasks[2].target_offset, 512);
        ASSERT_EQ(transfer_tasks[2].length, 512);

        // 清理资源
        for (auto ptr : ptrs)
        {
            free(ptr);
        }
        free(handle1->buffer);
        free(handle2->buffer);
    }

    TEST_F(DistributedObjectStoreGenerateTest, GenerateReplicaTransferRequests)
    {
        ReplicaInfo existed_replica_info;
        ReplicaInfo new_replica_info;
        std::vector<TransferRequest> transfer_tasks;

        // 创建一些模拟的 BufHandle
        std::shared_ptr<BufHandle> handle1 = std::make_shared<BufHandle>(nullptr, 1, 512, malloc(512));
        std::shared_ptr<BufHandle> handle2 = std::make_shared<BufHandle>(nullptr, 2, 1024, malloc(1024));
        existed_replica_info.handles = {handle1, handle2};

        std::shared_ptr<BufHandle> new_handle1 = std::make_shared<BufHandle>(nullptr, 3, 512, malloc(512));
        std::shared_ptr<BufHandle> new_handle2 = std::make_shared<BufHandle>(nullptr, 4, 1024, malloc(1024));
        new_replica_info.handles = {new_handle1, new_handle2};

        store->generateReplicaTransferRequests(existed_replica_info, new_replica_info, transfer_tasks);

        // 验证生成的 transfer_tasks 是否正确
        ASSERT_EQ(transfer_tasks.size(), 2);

        // 验证第一个请求
        ASSERT_EQ(transfer_tasks[0].opcode, TransferRequest::OpCode::WRITE);
        ASSERT_EQ(transfer_tasks[0].source, handle1->buffer);
        ASSERT_EQ(transfer_tasks[0].target_id, 3);
        ASSERT_EQ(transfer_tasks[0].target_offset, 0);
        ASSERT_EQ(transfer_tasks[0].length, 512);

        // 验证第二个请求
        ASSERT_EQ(transfer_tasks[1].opcode, TransferRequest::OpCode::WRITE);
        ASSERT_EQ(transfer_tasks[1].source, handle2->buffer);
        ASSERT_EQ(transfer_tasks[1].target_id, 4);
        ASSERT_EQ(transfer_tasks[1].target_offset, 0);
        ASSERT_EQ(transfer_tasks[1].length, 1024);

        // 清理资源
        free(handle1->buffer);
        free(handle2->buffer);
        free(new_handle1->buffer);
        free(new_handle2->buffer);
    }

} // namespace mooncake

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}