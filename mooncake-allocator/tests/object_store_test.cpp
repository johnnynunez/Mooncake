#include "distributed_object_store.h"
#include <cassert>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <malloc.h>
#include <vector>

using namespace mooncake;

class DistributedObjectStoreTest : public ::testing::Test
{
protected:
    DistributedObjectStore store;
    std::map<SegmentID, std::vector<uint64_t>> segment_and_index;

    void SetUp() override
    {
        for (int i = 0; i < 10; i++)
        {
            for (SegmentID segment_id = 1; segment_id <= 6; segment_id++)
            {
                segment_and_index[segment_id].push_back(testRegisterBuffer(store, segment_id));
            }

            // segment_and_index[1] = testRegisterBuffer(store, 1);
            // segment_and_index[2] = testRegisterBuffer(store, 2);
            // segment_and_index[3] = testRegisterBuffer(store, 3);
            // segment_and_index[4] = testRegisterBuffer(store, 4);
            // segment_and_index[5] = testRegisterBuffer(store, 5);
        }
    }

    void TearDown() override
    {
        for (auto &meta : segment_and_index)
        {
            // 暂时屏蔽
            for (size_t index = 0; index < meta.second.size(); ++index) {
                testUnregisterBuffer(store, meta.first, meta.second[index]);
            }
        }
        LOG(WARNING) << "finish teardown";
    }

    uint64_t testRegisterBuffer(DistributedObjectStore &store, SegmentId segmentId)
    {
        // size_t base = 0x100000000;
        size_t size = 1024 * 1024 * 4 * 200;
        void *ptr = nullptr;
        posix_memalign(&ptr, 4194304, size);
        size_t base = reinterpret_cast<size_t>(ptr);
        LOG(INFO) << "registerbuffer: " << (void *)base;
        uint64_t index = store.registerBuffer(segmentId, base, size);
        EXPECT_GE(index, 0);
        return index;
    }

    void testUnregisterBuffer(DistributedObjectStore &store, SegmentId segmentId, uint64_t index)
    {
        // 会触发recovery 到后面会无法recovery成功
        store.unregisterBuffer(segmentId, index);
    }
};

TEST_F(DistributedObjectStoreTest, PutGetTest)
{
    ObjectKey key = "test_object";
    std::vector<char> data(1024 * 1024, 'A');
    std::vector<void *> ptrs = {data.data()};
    std::vector<void *> sizes = {reinterpret_cast<void *>(data.size())};
    ReplicateConfig config;
    config.replica_num = 2;

    TaskID putVersion = store.put(key, ptrs, sizes, config);
    EXPECT_NE(putVersion, 0);

    std::vector<char> retrievedData(1024 * 1024);
    std::vector<void *> getPtrs = {retrievedData.data()};
    std::vector<void *> getSizes = {reinterpret_cast<void *>(retrievedData.size())};

    TaskID getVersion = store.get(key, getPtrs, getSizes, 0, 0);
    EXPECT_EQ(getVersion, putVersion);
}

char randomChar()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis('a', 'z'); // 假设随机字符的范围是 0-255
    return static_cast<char>(dis(gen));
}

void CompareAndLog(const std::vector<char> &combinedPutData, const std::vector<char> &combinedGetData, size_t offset, size_t compareSize)
{
    if (memcmp(combinedPutData.data() + offset, combinedGetData.data(), compareSize) != 0)
    {
        LOG(ERROR) << "Comparison failed: memcmp(combinedPutData.data() + " << offset << ", combinedGetData.data(), " << compareSize << ") != 0";
        LOG(ERROR) << "combinedPutData size: " << combinedPutData.size() - offset << ", content: " << std::string(combinedPutData.data() + offset, compareSize);
        LOG(ERROR) << "combinedGetData size: " << combinedGetData.size() << ",content: " << std::string(combinedGetData.data(), compareSize);
    }
}
TEST_F(DistributedObjectStoreTest, RandomSizePutGetTest)
{
    // 随机生成 data 的大小和 ptrs 的个数, put和get的ptrs size是相同大小的
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024 * 4); // 1 到 4MB 之间的随机大小
    std::uniform_int_distribution<> countDist(1, 10);             // 1 到 10 个 ptrs

    for (int testIteration = 0; testIteration < 10; ++testIteration)
    {
        int ptrCount = countDist(gen);
        std::vector<std::vector<char>> data(ptrCount);
        std::vector<void *> ptrs(ptrCount);
        std::vector<void *> sizes(ptrCount);

        for (int i = 0; i < ptrCount; ++i)
        {
            size_t size = sizeDist(gen);
            //size = (size / 1024) * 1024; // 确保大小是 1024 的倍数
            data[i].resize(size);
            std::generate(data[i].begin(), data[i].end(), randomChar);

            ptrs[i] = data[i].data();
            sizes[i] = reinterpret_cast<void *>(data[i].size());
        }

        ReplicateConfig config;
        config.replica_num = 3;

        // 生成一个新的 key
        ObjectKey key = "random_size_test_object_" + std::to_string(testIteration);
        TaskID putVersion = store.put(key, ptrs, sizes, config);
        EXPECT_NE(putVersion, 0);
        TaskID putVersion2 = store.put(key, ptrs, sizes, config);
        EXPECT_EQ(putVersion2, putVersion + 1);

        std::vector<std::vector<char>> retrievedData(ptrCount);
        std::vector<void *> getPtrs(ptrCount);
        std::vector<void *> getSizes(ptrCount);

        for (int i = 0; i < ptrCount; ++i)
        {
            size_t size = data[i].size();
            retrievedData[i].resize(size);
            getPtrs[i] = retrievedData[i].data();
            getSizes[i] = reinterpret_cast<void *>(retrievedData[i].size());
        }

        TaskID getVersion = store.get(key, getPtrs, getSizes, putVersion, 0);
        EXPECT_EQ(getVersion, putVersion2);

        for (int i = 0; i < ptrCount; ++i)
        {
            EXPECT_EQ(data[i].size(), retrievedData[i].size());
            CompareAndLog(data[i], retrievedData[i], 0, data[i].size());
            EXPECT_EQ(memcmp(data[i].data(), retrievedData[i].data(), data[i].size()), 0);
        }
    }
}

TEST_F(DistributedObjectStoreTest, RandomSizeOffsetPutGetTest)
{
    // 随机生成 data 的大小和 ptrs 的个数, put和get的ptrs size是不同大小的，而且包含了随机的offset参数

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024 * 4); // 1 to 4MB
    std::uniform_int_distribution<> countDist(1, 10);             // 1 to 10 ptrs
    std::uniform_int_distribution<> offsetDist(0, 1024 * 1024);   // 0 to 1MB offset

    for (int testIteration = 0; testIteration < 10; ++testIteration)
    {
        int ptrCount = countDist(gen);
        std::vector<std::vector<char>> data(ptrCount);
        std::vector<void *> putPtrs(ptrCount);
        std::vector<void *> putSizes(ptrCount);
        size_t totalSize = 0;

        // Prepare data for put operation
        for (int i = 0; i < ptrCount; ++i)
        {
            size_t size = sizeDist(gen);
            data[i].resize(size);
            std::generate(data[i].begin(), data[i].end(), randomChar);
            putPtrs[i] = data[i].data();
            putSizes[i] = reinterpret_cast<void *>(data[i].size());
            totalSize += size;
        }

        ReplicateConfig config;
        config.replica_num = 1;
        ObjectKey key = "random_size_offset_test_object_" + std::to_string(testIteration);
        TaskID putVersion = store.put(key, putPtrs, putSizes, config);
        EXPECT_NE(putVersion, 0);

        // Prepare data for get operation with different sizes
        int getPtrCount = countDist(gen);
        std::vector<std::vector<char>> retrievedData(getPtrCount);
        std::vector<void *> getPtrs(getPtrCount);
        std::vector<void *> getSizes(getPtrCount);
        size_t totalGetSize = 0;

        for (int i = 0; i < getPtrCount; ++i)
        {
            size_t size = sizeDist(gen);
            retrievedData[i].resize(size);
            getPtrs[i] = retrievedData[i].data();
            getSizes[i] = reinterpret_cast<void *>(retrievedData[i].size());
            totalGetSize += size;
        }

        // Generate random offset
        size_t offset = offsetDist(gen) % (totalSize / 2); // Ensure offset is not too large
        // size_t offset = 0;

        TaskID getVersion = store.get(key, getPtrs, getSizes, putVersion, offset);
        EXPECT_EQ(getVersion, putVersion);

        // Combine all put data into a single buffer for comparison
        std::vector<char> combinedPutData;
        for (const auto &d : data)
        {
            combinedPutData.insert(combinedPutData.end(), d.begin(), d.end());
        }

        // Combine all get data into a single buffer
        std::vector<char> combinedGetData;
        for (const auto &d : retrievedData)
        {
            combinedGetData.insert(combinedGetData.end(), d.begin(), d.end());
        }

        // Compare data
        size_t compareSize = std::min(totalSize - offset, totalGetSize);
        EXPECT_GE(combinedGetData.size(), compareSize);
        std::string putdata(combinedPutData.data() + offset, compareSize);
        std::string getdata(combinedGetData.data(), compareSize);
        LOG(INFO) << "the putdata, size: " << compareSize << " , content: " << putdata;
        LOG(INFO) << "the getdata, size: " << compareSize << " , content: " << putdata;
        CompareAndLog(combinedPutData, combinedGetData, offset, compareSize);

        EXPECT_EQ(memcmp(combinedPutData.data() + offset, combinedGetData.data(), compareSize), 0);
    }
}

TEST_F(DistributedObjectStoreTest, OverwriteExistingKeyTest)
{
    ObjectKey key = "existing_key_test_object";
    std::vector<char> initialData(1024, 'A');
    std::vector<void *> initialPtrs = {initialData.data()};
    std::vector<void *> initialSizes = {reinterpret_cast<void *>(initialData.size())};
    ReplicateConfig config;
    config.replica_num = 2;

    // 第一次 put
    TaskID initialVersion = store.put(key, initialPtrs, initialSizes, config);
    EXPECT_NE(initialVersion, 0);

    // 获取第一次 put 的数据
    std::vector<char> retrievedInitialData(1024);
    std::vector<void *> getInitialPtrs = {retrievedInitialData.data()};
    std::vector<void *> getInitialSizes = {reinterpret_cast<void *>(retrievedInitialData.size())};
    TaskID getInitialVersion = store.get(key, getInitialPtrs, getInitialSizes, 0, 0);
    EXPECT_EQ(getInitialVersion, initialVersion);
    EXPECT_EQ(memcmp(initialData.data(), retrievedInitialData.data(), initialData.size()), 0);

    // 第二次 put，覆盖已存在的 key
    std::vector<char> newData(1024, 'B');
    std::vector<void *> newPtrs = {newData.data()};
    std::vector<void *> newSizes = {reinterpret_cast<void *>(newData.size())};
    TaskID newVersion = store.put(key, newPtrs, newSizes, config);
    EXPECT_NE(newVersion, 0);
    EXPECT_NE(newVersion, initialVersion); // 确保版本号更新

    // 获取第二次 put 的数据
    std::vector<char> retrievedNewData(1024);
    std::vector<void *> getNewPtrs = {retrievedNewData.data()};
    std::vector<void *> getNewSizes = {reinterpret_cast<void *>(retrievedNewData.size())};
    TaskID getNewVersion = store.get(key, getNewPtrs, getNewSizes, newVersion, 0);
    EXPECT_EQ(getNewVersion, newVersion);
    EXPECT_EQ(memcmp(newData.data(), retrievedNewData.data(), newData.size()), 0);

    // 确保获取到的数据是新的数据
    EXPECT_NE(memcmp(retrievedInitialData.data(), retrievedNewData.data(), retrievedInitialData.size()), 0);
}

TEST_F(DistributedObjectStoreTest, RemoveAndPutTest)
{
    ObjectKey key = "remove_and_put_test_object";
    std::vector<char> initialData(1024, 'A');
    std::vector<void *> initialPtrs = {initialData.data()};
    std::vector<void *> initialSizes = {reinterpret_cast<void *>(initialData.size())};
    ReplicateConfig config;
    config.replica_num = 2;

    // 第一次 put
    TaskID initialVersion = store.put(key, initialPtrs, initialSizes, config);
    EXPECT_NE(initialVersion, 0);

    // 获取第一次 put 的数据
    std::vector<char> retrievedInitialData(1024);
    std::vector<void *> getInitialPtrs = {retrievedInitialData.data()};
    std::vector<void *> getInitialSizes = {reinterpret_cast<void *>(retrievedInitialData.size())};
    TaskID getInitialVersion = store.get(key, getInitialPtrs, getInitialSizes, 0, 0);
    EXPECT_EQ(getInitialVersion, initialVersion);
    EXPECT_EQ(memcmp(initialData.data(), retrievedInitialData.data(), initialData.size()), 0);

    // 移除 key
    TaskID removeVersion = store.remove(key);
    EXPECT_EQ(removeVersion, initialVersion);

    // 尝试获取已移除的 key，应该返回错误
    std::vector<char> retrievedRemovedData(1024);
    std::vector<void *> getRemovedPtrs = {retrievedRemovedData.data()};
    std::vector<void *> getRemovedSizes = {reinterpret_cast<void *>(retrievedRemovedData.size())};
    TaskID getRemovedVersion = store.get(key, getRemovedPtrs, getRemovedSizes, 0, 0);
    EXPECT_LT(getRemovedVersion, 0); // 期望返回错误

    // 再次 put 新的数据
    std::vector<char> newData(1024, 'B');
    std::vector<void *> newPtrs = {newData.data()};
    std::vector<void *> newSizes = {reinterpret_cast<void *>(newData.size())};
    TaskID newVersion = store.put(key, newPtrs, newSizes, config);
    EXPECT_NE(newVersion, 0);
    EXPECT_NE(newVersion, initialVersion); // 确保版本号更新

    // 获取新的数据
    std::vector<char> retrievedNewData(1024);
    std::vector<void *> getNewPtrs = {retrievedNewData.data()};
    std::vector<void *> getNewSizes = {reinterpret_cast<void *>(retrievedNewData.size())};
    TaskID getNewVersion = store.get(key, getNewPtrs, getNewSizes, newVersion, 0);
    EXPECT_EQ(getNewVersion, newVersion);
    EXPECT_EQ(memcmp(newData.data(), retrievedNewData.data(), newData.size()), 0);

    // 确保获取到的数据是新的数据
    EXPECT_NE(memcmp(retrievedInitialData.data(), retrievedNewData.data(), retrievedInitialData.size()), 0);
}

TEST_F(DistributedObjectStoreTest, ReplicateTest)
{
    ObjectKey key = "replicate_test_object";
    std::vector<char> data(2048, 'B');
    std::vector<void *> ptrs = {data.data()};
    std::vector<void *> sizes = {reinterpret_cast<void *>(data.size())};
    ReplicateConfig config;
    config.replica_num = 1;

    TaskID putVersion = store.put(key, ptrs, sizes, config);

    ReplicateConfig newConfig;
    newConfig.replica_num = 3;
    DistributedObjectStore::ReplicaDiff replicaDiff;

    TaskID replicateVersion = store.replicate(key, newConfig, replicaDiff);
    EXPECT_EQ(replicateVersion, putVersion);
}

TEST_F(DistributedObjectStoreTest, CheckAllTest)
{
    store.checkAll();
}

TEST_F(DistributedObjectStoreTest, EdgeCasesTest)
{
    // Test get with non-existent key
    ObjectKey nonExistentKey = "non_existent_key";
    std::vector<char> buffer(1024);
    std::vector<void *> ptrs = {buffer.data()};
    std::vector<void *> sizes = {reinterpret_cast<void *>(buffer.size())};

    TaskID getVersion = store.get(nonExistentKey, ptrs, sizes, 0, 0);
    EXPECT_LT(getVersion, 0);

    // Test put with zero-sized object
    ObjectKey zeroSizeKey = "zero_size_object";
    std::vector<void *> zeroPtrs = {nullptr};
    std::vector<void *> zeroSizes = {nullptr};
    ReplicateConfig config;
    config.replica_num = 1;

    TaskID putVersion = store.put(zeroSizeKey, zeroPtrs, zeroSizes, config);
    EXPECT_NE(putVersion, 0);
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging("test_log");
    //google::SetLogDestination(google::INFO, "logs/log_info_");
    google::SetLogDestination(google::WARNING, "logs/log_info_");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
