#include "buffer_allocator.h"
#include "random_allocation_strategy.h"
#include "replica_allocator.h"
#include <gtest/gtest.h>

using namespace mooncake;

class ReplicaAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize ReplicaAllocator with shard size of 1024 bytes
        allocator = std::make_shared<ReplicaAllocator>(shard_size);

        // Register some buffers
        size_t base = 0x100000000;
        size_t size = 1024 * 1024 * 4;
        SegmentId segment_id = 1;
        uint64_t allocator_index = allocator->registerBuffer(segment_id, base, size * 2);
        segment_buffer_index[segment_id] = allocator_index;

        segment_id = 2;
        allocator_index = allocator->registerBuffer(segment_id, base + size * 2, size * 2);
        segment_buffer_index[segment_id] = allocator_index;

        segment_id = 3;
        allocator_index = allocator->registerBuffer(segment_id, base + size * 4, size * 4);
        segment_buffer_index[segment_id] = allocator_index;
    }

    std::shared_ptr<ReplicaAllocator> allocator;
    std::map<SegmentId, uint64_t> segment_buffer_index;
    size_t shard_size = 1024;
};

TEST_F(ReplicaAllocatorTest, RegisterBuffer)
{
    SegmentId segment_id = 1;
    size_t base = 0x110000000;
    size_t size = 1024 * 1024 * 8;
    uint64_t origin_index = segment_buffer_index[segment_id];
    uint64_t index = allocator->registerBuffer(segment_id, base, size);
    EXPECT_EQ(index, origin_index + 1);
}

TEST_F(ReplicaAllocatorTest, AddOneReplica_Valid)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = shard_size;

    Version result = allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);
    EXPECT_EQ(result, ver);
    EXPECT_EQ(ret.status, ReplicaStatus::INITIALIZED);
    EXPECT_EQ(ret.handles.size(), object_size / shard_size);
    EXPECT_EQ(ret.replica_id, 0);

    ReplicaInfo ret2;
    Version result2 = allocator->addOneReplica(key, ret2, ver, object_size);
    EXPECT_EQ(result, result2);
    EXPECT_EQ(ret.status, ret2.status);
    EXPECT_EQ(ret.handles.size(), ret2.handles.size());
    EXPECT_EQ(ret2.replica_id, 1);

    key = "test_key_2";
    object_size = shard_size * 4 + 512;
    result = allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);
    EXPECT_EQ(ret.handles.size(), object_size / shard_size + 1);

    key = "test_key_3";
    object_size = shard_size / 2;
    result = allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);
    EXPECT_EQ(ret.handles.size(), object_size / shard_size + 1);
}

TEST_F(ReplicaAllocatorTest, AddOneReplica_InvalidArgs)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = DEFAULT_VALUE;
    size_t object_size = DEFAULT_VALUE;

    EXPECT_THROW(allocator->addOneReplica(key, ret, ver, object_size), std::invalid_argument);
}

TEST_F(ReplicaAllocatorTest, GetOneReplica_Valid)
{
    ObjectKey key = "test_get_key";
    ReplicaInfo ret;
    size_t object_size = 1024;

    Version ver = allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);

    ReplicaInfo get_ret;
    Version get_result = allocator->getOneReplica(key, get_ret, ver);
    // add时只分配空间 还没写入数据，无法获取
    EXPECT_EQ(get_result, getError(ERRNO::INVALID_VERSION));
    EXPECT_EQ(get_ret.status, ReplicaStatus::FAILED);
    EXPECT_EQ(get_ret.handles.size(), 0);

    // 更新为完成
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }

    get_result = allocator->getOneReplica(key, get_ret, ver);
    EXPECT_EQ(get_ret.status, ReplicaStatus::COMPLETE);
    EXPECT_EQ(get_ret.handles.size(), 1);
}

TEST_F(ReplicaAllocatorTest, GetOneReplica_InvalidKey)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    ReplicaInfo get_ret;
    ObjectKey invalid_key="test_key_invalid";
    Version get_result = allocator->getOneReplica(invalid_key, get_ret, ver);
    EXPECT_EQ(get_result, getError(ERRNO::INVALID_KEY));
}

TEST_F(ReplicaAllocatorTest, GetOneReplica_InvalidVersion)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    ReplicaInfo get_ret;
    Version invalid_ver = 2;
    Version get_result = allocator->getOneReplica(key, get_ret, invalid_ver);
    EXPECT_EQ(get_result, getError(ERRNO::INVALID_VERSION));
}

TEST_F(ReplicaAllocatorTest, ReassignReplica)
{
    ObjectKey key = "test_key_ressign";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);

    ReplicaInfo reassigned_ret;
    allocator->reassignReplica(key, ver, ret.replica_id, reassigned_ret);
    EXPECT_EQ(reassigned_ret.status, ReplicaStatus::INITIALIZED);
    EXPECT_EQ(reassigned_ret.handles.size(), 1);

    // 更新为完成
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, ret.replica_id, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }
    ver = allocator->getOneReplica(key, reassigned_ret, ver);
    EXPECT_EQ(reassigned_ret.status, ReplicaStatus::COMPLETE);
}

TEST_F(ReplicaAllocatorTest, RemoveOneReplica)
{
    ObjectKey key = "test_key_remove";
    ReplicaInfo ret;
    size_t object_size = 1024;

    Version ver = allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }

    ver = allocator->addOneReplica(key, ret, ver, DEFAULT_VALUE);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 1, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }

    ReplicaInfo removed_ret;
    allocator->removeOneReplica(key, removed_ret, ver);
    EXPECT_EQ(removed_ret.status, ReplicaStatus::REMOVED);
    EXPECT_EQ(removed_ret.handles.size(), 1);
}

TEST_F(ReplicaAllocatorTest, RemoveOneReplicaInvalidVersion)
{
    ObjectKey key = "test_key_remove";
    ReplicaInfo ret;
    size_t object_size = 1024;

    Version ver = allocator->addOneReplica(key, ret, -1, object_size);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }

    ver = allocator->addOneReplica(key, ret, ver, -1);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 1, ver);
    for (auto &handle : ret.handles)
    {
        handle->status = BufStatus::COMPLETE;
    }

    ReplicaInfo removed_ret;
    re = allocator->removeOneReplica(key, removed_ret, ver+1);
    EXPECT_EQ(re, getError(ERRNO::INVALID_VERSION));
}

TEST_F(ReplicaAllocatorTest, Unregister)
{
    SegmentId segment_id = 1;
    size_t base = 0x100000000 + 1024 * 1024 * 100;
    size_t size = 1024 * 1024 * 4;

    int index = allocator->registerBuffer(segment_id, base, size);
    std::vector<std::shared_ptr<BufHandle>> handles = allocator->unregister(segment_id, index);
    EXPECT_EQ(handles.size(), 0);
}

TEST_F(ReplicaAllocatorTest, Recovery)
{
    ObjectKey key = "test_key_recovery";
    ReplicaInfo ret;
    size_t object_size = 1024 * 10 + 512;

    allocator->addOneReplica(key, ret, DEFAULT_VALUE, object_size);

    std::vector<std::shared_ptr<BufHandle>> old_handles = ret.handles;
    size_t new_num = allocator->recovery(old_handles);

    EXPECT_EQ(old_handles.size(), object_size / shard_size + 1);
    EXPECT_EQ(old_handles.size(), new_num);
}

TEST_F(ReplicaAllocatorTest, Checkall)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    std::vector<std::shared_ptr<BufHandle>> handles = allocator->checkall();
    EXPECT_EQ(handles.size(), 0);
}

TEST_F(ReplicaAllocatorTest, IfExist)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    EXPECT_TRUE(allocator->ifExist(key));
    EXPECT_FALSE(allocator->ifExist("non_existent_key"));
}

TEST_F(ReplicaAllocatorTest, UpdateStatus)
{
    ObjectKey key = "test_key_update";
    ReplicaInfo ret;
    Version ver = DEFAULT_VALUE;
    size_t object_size = 1024;

    ver = allocator->addOneReplica(key, ret, ver, object_size);

    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    VersionList &version_list = allocator->getObjectMeta()[key];
    EXPECT_EQ(version_list.versions[ver].replicas[ret.replica_id].status, ReplicaStatus::COMPLETE);
}

TEST_F(ReplicaAllocatorTest, GetObjectVersion)
{
    ObjectKey key = "test_key_version";
    ReplicaInfo ret;
    Version ver = DEFAULT_VALUE;
    size_t object_size = 1024;

    ver = allocator->addOneReplica(key, ret, ver, object_size);

    Version result = allocator->getObjectVersion(key);
    EXPECT_EQ(result, DEFAULT_VALUE);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    result = allocator->getObjectVersion(key);
    EXPECT_EQ(result, ver);
}

TEST_F(ReplicaAllocatorTest, GetObjectReplicaConfig)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    ReplicateConfig config = allocator->getObjectReplicaConfig(key);
    EXPECT_EQ(config.replica_num, 0);
}

TEST_F(ReplicaAllocatorTest, GetReplicaRealNumber)
{
    ObjectKey key = "test_key";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    allocator->addOneReplica(key, ret, ver, object_size);

    size_t real_number = allocator->getReplicaRealNumber(key, ver);
    EXPECT_EQ(real_number, 0);

    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 0, ver);
    real_number = allocator->getReplicaRealNumber(key, ver);
    EXPECT_EQ(real_number, 1);
}

TEST_F(ReplicaAllocatorTest, CleanUncompleteReplica)
{
    ObjectKey key = "test_key_cleanup";
    ReplicaInfo ret;
    Version ver = 1;
    size_t object_size = 1024;

    ver = allocator->addOneReplica(key, ret, ver, object_size);
    allocator->addOneReplica(key, ret, ver, object_size);

    allocator->updateStatus(key, ReplicaStatus::PARTIAL, 0, ver);
    size_t clean_num = allocator->cleanUncompleteReplica(key, ver, 1); // 最多保留一个副本
    EXPECT_EQ(clean_num, 1);                                           // 删除一个

    allocator->addOneReplica(key, ret, ver, object_size);
    allocator->updateStatus(key, ReplicaStatus::COMPLETE, 2, ver);
    clean_num = allocator->cleanUncompleteReplica(key, ver, 2); // 最多保留两个副本
    EXPECT_EQ(clean_num, 0);                                    // 删除零个

    clean_num = allocator->cleanUncompleteReplica(key, ver, 0); // 最多保留零个副本
    EXPECT_EQ(clean_num, 0);                                    // 实现中 保留的副本个数最少应该为完整副本数量

    size_t real_number = allocator->getReplicaRealNumber(key, ver);
    EXPECT_EQ(real_number, 1);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
