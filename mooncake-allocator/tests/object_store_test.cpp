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
            for (size_t index = 0; index < meta.second.size(); ++index)
            {
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
        int result = posix_memalign(&ptr, 4194304, size);
        if (result != 0)
        {
            perror("posix_memalign failed");
            return 0;
        }
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
    std::vector<Slice> slices = {{data.data(), data.size()}};
    ReplicateConfig config;
    config.replica_num = 2;

    TaskID putVersion = store.put(key, slices, config);
    EXPECT_NE(putVersion, 0);

    std::vector<char> retrievedData(1024 * 1024);
    std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};

    TaskID getVersion = store.get(key, getSlices, 0, 0);
    EXPECT_EQ(getVersion, putVersion);
    EXPECT_EQ(memcmp(data.data(), retrievedData.data(), data.size()), 0);
}

char randomChar()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis('a', 'z');
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
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024 * 4);
    std::uniform_int_distribution<> countDist(1, 10);

    for (int testIteration = 0; testIteration < 10; ++testIteration)
    {
        int sliceCount = countDist(gen);
        std::vector<std::vector<char>> data(sliceCount);
        std::vector<Slice> slices(sliceCount);

        for (int i = 0; i < sliceCount; ++i)
        {
            size_t size = sizeDist(gen);
            data[i].resize(size);
            std::generate(data[i].begin(), data[i].end(), randomChar);

            slices[i] = {data[i].data(), data[i].size()};
        }

        ReplicateConfig config;
        config.replica_num = 3;

        ObjectKey key = "random_size_test_object_" + std::to_string(testIteration);
        TaskID putVersion = store.put(key, slices, config);
        EXPECT_NE(putVersion, 0);
        TaskID putVersion2 = store.put(key, slices, config);
        EXPECT_EQ(putVersion2, putVersion + 1);

        std::vector<std::vector<char>> retrievedData(sliceCount);
        std::vector<Slice> getSlices(sliceCount);

        for (int i = 0; i < sliceCount; ++i)
        {
            size_t size = data[i].size();
            retrievedData[i].resize(size);
            getSlices[i] = {retrievedData[i].data(), retrievedData[i].size()};
        }

        TaskID getVersion = store.get(key, getSlices, putVersion, 0);
        EXPECT_EQ(getVersion, putVersion2);

        for (int i = 0; i < sliceCount; ++i)
        {
            EXPECT_EQ(data[i].size(), retrievedData[i].size());
            CompareAndLog(data[i], retrievedData[i], 0, data[i].size());
            EXPECT_EQ(memcmp(data[i].data(), retrievedData[i].data(), data[i].size()), 0);
        }
    }
}

TEST_F(DistributedObjectStoreTest, RandomSizeOffsetPutGetTest)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024 * 4);
    std::uniform_int_distribution<> countDist(1, 10);
    std::uniform_int_distribution<> offsetDist(0, 1024 * 1024);

    for (int testIteration = 0; testIteration < 10; ++testIteration)
    {
        int putSliceCount = countDist(gen);
        std::vector<std::vector<char>> data(putSliceCount);
        std::vector<Slice> putSlices(putSliceCount);
        size_t totalSize = 0;

        for (int i = 0; i < putSliceCount; ++i)
        {
            size_t size = sizeDist(gen);
            data[i].resize(size);
            std::generate(data[i].begin(), data[i].end(), randomChar);
            putSlices[i] = {data[i].data(), data[i].size()};
            totalSize += size;
        }

        ReplicateConfig config;
        config.replica_num = 1;
        ObjectKey key = "random_size_offset_test_object_" + std::to_string(testIteration);
        TaskID putVersion = store.put(key, putSlices, config);
        EXPECT_NE(putVersion, 0);

        int getSliceCount = countDist(gen);
        std::vector<std::vector<char>> retrievedData(getSliceCount);
        std::vector<Slice> getSlices(getSliceCount);
        size_t totalGetSize = 0;

        for (int i = 0; i < getSliceCount; ++i)
        {
            size_t size = sizeDist(gen);
            retrievedData[i].resize(size);
            getSlices[i] = {retrievedData[i].data(), retrievedData[i].size()};
            totalGetSize += size;
        }

        size_t offset = offsetDist(gen) % (totalSize / 2);

        TaskID getVersion = store.get(key, getSlices, putVersion, offset);
        EXPECT_EQ(getVersion, putVersion);

        std::vector<char> combinedPutData;
        for (const auto &d : data)
        {
            combinedPutData.insert(combinedPutData.end(), d.begin(), d.end());
        }

        std::vector<char> combinedGetData;
        for (const auto &d : retrievedData)
        {
            combinedGetData.insert(combinedGetData.end(), d.begin(), d.end());
        }

        size_t compareSize = std::min(totalSize - offset, totalGetSize);
        EXPECT_GE(combinedGetData.size(), compareSize);
        std::string putdata(combinedPutData.data() + offset, compareSize);
        std::string getdata(combinedGetData.data(), compareSize);
        LOG(INFO) << "the putdata, size: " << compareSize << " , content: " << putdata;
        LOG(INFO) << "the getdata, size: " << compareSize << " , content: " << getdata;
        CompareAndLog(combinedPutData, combinedGetData, offset, compareSize);

        EXPECT_EQ(memcmp(combinedPutData.data() + offset, combinedGetData.data(), compareSize), 0);
    }
}

TEST_F(DistributedObjectStoreTest, OverwriteExistingKeyTest)
{
    ObjectKey key = "existing_key_test_object";
    std::vector<char> initialData(1024, 'A');
    std::vector<Slice> initialSlices = {{initialData.data(), initialData.size()}};
    ReplicateConfig config;
    config.replica_num = 2;

    TaskID initialVersion = store.put(key, initialSlices, config);
    EXPECT_NE(initialVersion, 0);

    std::vector<char> retrievedInitialData(1024);
    std::vector<Slice> getInitialSlices = {{retrievedInitialData.data(), retrievedInitialData.size()}};
    TaskID getInitialVersion = store.get(key, getInitialSlices, 0, 0);
    EXPECT_EQ(getInitialVersion, initialVersion);
    EXPECT_EQ(memcmp(initialData.data(), retrievedInitialData.data(), initialData.size()), 0);

    std::vector<char> newData(1024, 'B');
    std::vector<Slice> newSlices = {{newData.data(), newData.size()}};
    TaskID newVersion = store.put(key, newSlices, config);
    EXPECT_NE(newVersion, 0);
    EXPECT_NE(newVersion, initialVersion);

    std::vector<char> retrievedNewData(1024);
    std::vector<Slice> getNewSlices = {{retrievedNewData.data(), retrievedNewData.size()}};
    TaskID getNewVersion = store.get(key, getNewSlices, newVersion, 0);
    EXPECT_EQ(getNewVersion, newVersion);
    EXPECT_EQ(memcmp(newData.data(), retrievedNewData.data(), newData.size()), 0);

    EXPECT_NE(memcmp(retrievedInitialData.data(), retrievedNewData.data(), retrievedInitialData.size()), 0);
}

TEST_F(DistributedObjectStoreTest, RemoveAndPutTest)
{
    ObjectKey key = "remove_and_put_test_object";
    std::vector<char> initialData(1024, 'A');
    std::vector<Slice> initialSlices = {{initialData.data(), initialData.size()}};
    ReplicateConfig config;
    config.replica_num = 2;

    TaskID initialVersion = store.put(key, initialSlices, config);
    EXPECT_NE(initialVersion, 0);

    std::vector<char> retrievedInitialData(1024);
    std::vector<Slice> getInitialSlices = {{retrievedInitialData.data(), retrievedInitialData.size()}};
    TaskID getInitialVersion = store.get(key, getInitialSlices, 0, 0);
    EXPECT_EQ(getInitialVersion, initialVersion);
    EXPECT_EQ(memcmp(initialData.data(), retrievedInitialData.data(), initialData.size()), 0);

    TaskID removeVersion = store.remove(key);
    EXPECT_EQ(removeVersion, initialVersion);

    std::vector<char> retrievedRemovedData(1024);
    std::vector<Slice> getRemovedSlices = {{retrievedRemovedData.data(), retrievedRemovedData.size()}};
    TaskID getRemovedVersion = store.get(key, getRemovedSlices, 0, 0);
    EXPECT_LT(getRemovedVersion, 0);

    std::vector<char> newData(1024, 'B');
    std::vector<Slice> newSlices = {{newData.data(), newData.size()}};
    TaskID newVersion = store.put(key, newSlices, config);
    EXPECT_NE(newVersion, 0);
    EXPECT_NE(newVersion, initialVersion);

    std::vector<char> retrievedNewData(1024);
    std::vector<Slice> getNewSlices = {{retrievedNewData.data(), retrievedNewData.size()}};
    TaskID getNewVersion = store.get(key, getNewSlices, newVersion, 0);
    EXPECT_EQ(getNewVersion, newVersion);
    EXPECT_EQ(memcmp(newData.data(), retrievedNewData.data(), newData.size()), 0);

    EXPECT_NE(memcmp(retrievedInitialData.data(), retrievedNewData.data(), retrievedInitialData.size()), 0);
}

TEST_F(DistributedObjectStoreTest, ReplicateTest)
{
    ObjectKey key = "replicate_test_object";
    std::vector<char> data(2048, 'B');
    std::vector<Slice> slices = {{data.data(), data.size()}};
    ReplicateConfig config;
    config.replica_num = 1;

    TaskID putVersion = store.put(key, slices, config);

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
    ObjectKey nonExistentKey = "non_existent_key";
    std::vector<char> buffer(1024);
    std::vector<Slice> slices = {{buffer.data(), buffer.size()}};

    TaskID getVersion = store.get(nonExistentKey, slices, 0, 0);
    EXPECT_LT(getVersion, 0);

    ObjectKey zeroSizeKey = "zero_size_object";
    std::vector<Slice> zeroSlices = {{nullptr, 0}};
    ReplicateConfig config;
    config.replica_num = 1;

    TaskID putVersion = store.put(zeroSizeKey, zeroSlices, config);
    EXPECT_NE(putVersion, 0);
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging("test_log");
    google::SetLogDestination(google::WARNING, "logs/log_info_");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}