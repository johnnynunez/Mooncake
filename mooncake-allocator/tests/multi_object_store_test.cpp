#include "distributed_object_store.h"
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace mooncake;

char randomChar()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis('a', 'z'); // 假设随机字符的范围是 0-255
    return static_cast<char>(dis(gen));
}

class DistributedObjectStoreMultiThreadTest : public ::testing::Test
{
protected:
    DistributedObjectStore store;
    std::map<SegmentID, std::vector<uint64_t>> segment_and_index;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> completed_threads{0};
    std::atomic<bool> start_flag{false};

    void SetUp() override
    {
        for (int i = 0; i < 10; i++)
        {
            for (SegmentID segment_id = 1; segment_id <= 6; segment_id++)
            {
                segment_and_index[segment_id].push_back(testRegisterBuffer(store, segment_id));
            }
        }
    }

    void TearDown() override
    {
        for (auto &meta : segment_and_index)
        {
            // 暂时屏蔽
            //testUnregisterBuffer(store, meta.first, meta.second);
        }
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

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentPutTest)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<ObjectKey> keys(numThreads);
    std::vector<std::vector<char>> data(numThreads);
    std::vector<ReplicateConfig> configs(numThreads);
    std::vector<TaskID> versions(numThreads);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    for (int i = 0; i < numThreads; ++i)
    {
        keys[i] = "test_object_" + std::to_string(i);
        data[i].resize(sizeDist(gen));
        std::generate(data[i].begin(), data[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]() {
            std::vector<void *> ptrs = {data[i].data()};
            std::vector<void *> sizes = {reinterpret_cast<void *>(data[i].size())};
            versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
            EXPECT_NE(versions[i], 0);
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    for (int i = 1; i < numThreads; ++i)
    {
        EXPECT_GE(versions[i], 0);
    }

    // 对versions排序，判断应该是依次顺序增长
    std::sort(versions.begin(), versions.end());
    for (int i = 1; i < numThreads; ++i)
    {
        EXPECT_EQ(versions[i], versions[i - 1] + 1);
    }
}

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentGetTest)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<ObjectKey> keys(numThreads);
    std::vector<std::vector<char>> data(numThreads);
    std::vector<ReplicateConfig> configs(numThreads);
    std::vector<TaskID> versions(numThreads);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    for (int i = 0; i < numThreads; ++i)
    {
        keys[i] = "test_object_" + std::to_string(i);
        data[i].resize(sizeDist(gen));
        std::generate(data[i].begin(), data[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        std::vector<void *> ptrs = {data[i].data()};
        std::vector<void *> sizes = {reinterpret_cast<void *>(data[i].size())};
        versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
        EXPECT_NE(versions[i], 0);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &versions, i]() {
            std::vector<char> retrievedData(data[i].size());
            std::vector<void *> getPtrs = {retrievedData.data()};
            std::vector<void *> getSizes = {reinterpret_cast<void *>(retrievedData.size())};
            TaskID getVersion = store.get(keys[i], getPtrs, getSizes, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData);
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }
}

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentPutAndGetTest)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<ObjectKey> keys(numThreads);
    std::vector<std::vector<char>> data(numThreads);
    std::vector<ReplicateConfig> configs(numThreads);
    std::vector<TaskID> versions(numThreads);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    for (int i = 0; i < numThreads; ++i)
    {
        keys[i] = "test_object_" + std::to_string(i);
        data[i].resize(sizeDist(gen));
        std::generate(data[i].begin(), data[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]() {
            std::vector<void *> ptrs = {data[i].data()};
            std::vector<void *> sizes = {reinterpret_cast<void *>(data[i].size())};
            versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
            EXPECT_NE(versions[i], 0);

            std::vector<char> retrievedData(data[i].size());
            std::vector<void *> getPtrs = {retrievedData.data()};
            std::vector<void *> getSizes = {reinterpret_cast<void *>(retrievedData.size())};
            TaskID getVersion = store.get(keys[i], getPtrs, getSizes, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData);
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }
}

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentRemoveAndPutTest)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<ObjectKey> keys(numThreads);
    std::vector<std::vector<char>> data(numThreads);
    std::vector<ReplicateConfig> configs(numThreads);
    std::vector<TaskID> versions(numThreads);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    for (int i = 0; i < numThreads; ++i)
    {
        keys[i] = "test_object_removeandput_" + std::to_string(i);
        data[i].resize(sizeDist(gen));
        std::generate(data[i].begin(), data[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]() {
            std::vector<void *> ptrs = {data[i].data()};
            std::vector<void *> sizes = {reinterpret_cast<void *>(data[i].size())};
            versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
            EXPECT_NE(versions[i], 0);

            TaskID removeVersion = store.remove(keys[i], versions[i]);
            EXPECT_EQ(removeVersion, versions[i]);

            std::vector<char> retrievedData(data[i].size());
            std::vector<void *> getPtrs = {retrievedData.data()};
            std::vector<void *> getSizes = {reinterpret_cast<void *>(retrievedData.size())};
            TaskID getVersion = store.get(keys[i], getPtrs, getSizes, versions[i], 0);
            EXPECT_LT(getVersion, 0);

            versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
            EXPECT_NE(versions[i], 0);

            getVersion = store.get(keys[i], getPtrs, getSizes, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData);
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }
}

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentReplicateAndGetTest)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<ObjectKey> keys(numThreads);
    std::vector<std::vector<char>> data(numThreads);
    std::vector<ReplicateConfig> configs(numThreads);
    std::vector<TaskID> versions(numThreads);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    for (int i = 0; i < numThreads; ++i)
    {
        keys[i] = "test_object_" + std::to_string(i);
        data[i].resize(sizeDist(gen));
        std::generate(data[i].begin(), data[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        std::vector<void *> ptrs = {data[i].data()};
        std::vector<void *> sizes = {reinterpret_cast<void *>(data[i].size())};
        versions[i] = store.put(keys[i], ptrs, sizes, configs[i]);
        EXPECT_NE(versions[i], 0);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]() {
            ReplicateConfig newConfig;
            newConfig.replica_num = configs[i].replica_num + 1;
            DistributedObjectStore::ReplicaDiff replicaDiff;
            TaskID replicateVersion = store.replicate(keys[i], newConfig, replicaDiff);
            EXPECT_EQ(replicateVersion, versions[i]);

            std::vector<char> retrievedData(data[i].size());
            std::vector<void *> getPtrs = {retrievedData.data()};
            std::vector<void *> getSizes = {reinterpret_cast<void *>(retrievedData.size())};
            TaskID getVersion = store.get(keys[i], getPtrs, getSizes, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData);
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging("test_log");
    // google::SetLogDestination(google::INFO, "logs/log_info_");
    google::SetLogDestination(google::WARNING, "logs/log_warning_");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}