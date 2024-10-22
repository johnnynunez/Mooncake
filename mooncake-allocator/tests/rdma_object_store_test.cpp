#include "distributed_object_store.h"
#include <cassert>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <malloc.h>
#include <vector>

using namespace mooncake;

class RdmaDistributedObjectStoreTest : public ::testing::Test
{
protected:
    DistributedObjectStore store;
    std::map<SegmentID, std::vector<uint64_t>> segment_and_index;

    void SetUp() override
    {
        SegmentId segment = store.openSegment("optane08");
        testRegisterBuffer(store, segment);
    }

    void TearDown() override
    {
        // for (auto &meta : segment_and_index)
        // {
        //     // 暂时屏蔽
        //     for (int index = 0; index < meta.second.size(); ++index) {
        //         testUnregisterBuffer(store, meta.first, meta.second[index]);
        //     }
        // }
        LOG(WARNING) << "finish teardown";
    }

    uint64_t testRegisterBuffer(DistributedObjectStore &store, SegmentId segmentId)
    {
        size_t base = 0x40000000000; // 远程base地址
        size_t size = 1ull << 30;    // 远程大小
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

TEST_F(RdmaDistributedObjectStoreTest, PutGetTest)
{
    ObjectKey key = "test_object";

    // 获取本地地址
    size_t dataSize = 1024 * 10;
    void *dataPtr = store.allocateLocalMemory(dataSize);

    // 在 dataPtr 指向的空间中生成随机字符串
    std::generate(static_cast<char *>(dataPtr), static_cast<char *>(dataPtr) + dataSize, []()
                  { return 'A' + rand() % 26; });
    static_cast<char *>(dataPtr)[dataSize - 1] = '\0';

    // 创建 Slice 结构体
    Slice slice;
    slice.ptr = dataPtr;
    slice.size = dataSize;
    std::vector<Slice> slices = {slice};

    ReplicateConfig config;
    config.replica_num = 1;

    // 存储数据
    TaskID putVersion = store.put(key, slices, config);
    EXPECT_LT(putVersion, DEFAULT_VALUE);

    LOG(ERROR) << "finish put.";
    // sleep(1);

    TASK_STATUS status = store.getTaskStatus(putVersion);

    // loop utile all status all completed
    while (true)
    {
        status = store.getTaskStatus(putVersion);
        if (status.first == OperationStatus::COMPLETE)
        {
            break;
        }
        else
        {
            sleep(1);
        }
    }

    // 获取数据
    void *getPtr = store.allocateLocalMemory(dataSize);
    Slice getSlice;
    getSlice.ptr = getPtr;
    getSlice.size = dataSize;
    std::vector<Slice> getSlices = {getSlice};

    TaskID getVersion = store.get(key, getSlices, 0, 0);
    LOG(INFO) << "the getVersion: " << getVersion;
    // 比较原始数据和获取的数据
    char *retrievedDataPtr = static_cast<char *>(getPtr);
    retrievedDataPtr[dataSize - 1] = '\0';
    EXPECT_EQ(std::memcmp(dataPtr, retrievedDataPtr, dataSize), 0);
    // LOG(ERROR) << "put data: "  << static_cast<char*>(dataPtr);
    // LOG(ERROR) << "get data: " << retrievedDataPtr;
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
int main(int argc, char **argv)
{
    google::InitGoogleLogging("test_log");
    google::SetLogDestination(google::INFO, "logs/log_info_");
    // google::SetLogDestination(google::WARNING, "logs/log_info_");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
