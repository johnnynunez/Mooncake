#include "distributed_object_store.h"
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace mooncake;

thread_local std::random_device rd;
thread_local std::mt19937 gen(rd());
thread_local std::uniform_int_distribution<> dis('a', 'z');

char randomChar()
{
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
            LOG(INFO) << "meta info, segment_id: " << meta.first << ", buffer size: " << meta.second.size();
            // Temporarily disabled
            // testUnregisterBuffer(store, meta.first, meta.second);
        }
    }

    uint64_t testRegisterBuffer(DistributedObjectStore &store, SegmentId segmentId)
    {
        size_t size = 1024 * 1024 * 4 * 200;
        void *ptr = store.allocateLocalMemory(size);
        LOG(INFO) << "registerbuffer: " << ptr;
        uint64_t index = store.registerBuffer(segmentId, reinterpret_cast<size_t>(ptr), size);
        EXPECT_GE(index, 0);
        return index;
    }

    void testUnregisterBuffer(DistributedObjectStore &store, SegmentId segmentId, uint64_t index)
    {
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
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]()
                             {
            std::vector<Slice> slices = {{data[i].data(), data[i].size()}};
            versions[i] = store.put(keys[i], slices, configs[i]);
            EXPECT_NE(versions[i], 0); });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    for (int i = 1; i < numThreads; ++i)
    {
        EXPECT_GE(versions[i], 0);
    }

    // Sort versions and check if they are incrementing
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
        std::vector<Slice> slices = {{data[i].data(), data[i].size()}};
        versions[i] = store.put(keys[i], slices, configs[i]);
        EXPECT_NE(versions[i], 0);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &versions, i]()
                             {
            std::vector<char> retrievedData(data[i].size());
            std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};
            TaskID getVersion = store.get(keys[i], getSlices, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData); });
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
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]()
                             {
            std::vector<Slice> putSlices = {{data[i].data(), data[i].size()}};
            versions[i] = store.put(keys[i], putSlices, configs[i]);
            EXPECT_NE(versions[i], 0);

            std::vector<char> retrievedData(data[i].size());
            std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};
            TaskID getVersion = store.get(keys[i], getSlices, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData); });
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
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]()
                             {
            std::vector<Slice> putSlices = {{data[i].data(), data[i].size()}};
            versions[i] = store.put(keys[i], putSlices, configs[i]);
            EXPECT_NE(versions[i], 0);

            TaskID removeVersion = store.remove(keys[i], versions[i]);
            EXPECT_EQ(removeVersion, versions[i]);

            std::vector<char> retrievedData(data[i].size());
            std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};
            TaskID getVersion = store.get(keys[i], getSlices, versions[i], 0);
            EXPECT_LT(getVersion, 0);

            versions[i] = store.put(keys[i], putSlices, configs[i]);
            EXPECT_NE(versions[i], 0);

            getVersion = store.get(keys[i], getSlices, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData); });
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
        std::vector<Slice> putSlices = {{data[i].data(), data[i].size()}};
        versions[i] = store.put(keys[i], putSlices, configs[i]);
        EXPECT_NE(versions[i], 0);
    }

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &data, &configs, &versions, i]()
                             {
            ReplicateConfig newConfig;
            newConfig.replica_num = configs[i].replica_num + 1;
            DistributedObjectStore::ReplicaDiff replicaDiff;
            TaskID replicateVersion = store.replicate(keys[i], newConfig, replicaDiff);
            EXPECT_EQ(replicateVersion, versions[i]);

            std::vector<char> retrievedData(data[i].size());
            std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};
            TaskID getVersion = store.get(keys[i], getSlices, versions[i], 0);
            EXPECT_EQ(getVersion, versions[i]);
            EXPECT_EQ(data[i], retrievedData); });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }
}

TEST_F(DistributedObjectStoreMultiThreadTest, ConcurrentMixedOperationsTest)
{
    const int numThreads = 20;
    const int numKeys = 300;
    const int numOperationsPerThread = 1000;

    std::vector<ObjectKey> keys(numKeys);
    std::vector<std::vector<char>> initialData(numKeys);
    std::vector<ReplicateConfig> configs(numKeys);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(1, 1024 * 1024); // 1 to 1MB
    std::uniform_int_distribution<> replicaDist(1, 3);        // 1 to 3 replicas

    // Initialize keys and initial data
    for (int i = 0; i < numKeys; ++i)
    {
        keys[i] = "test_object_multithread_" + std::to_string(i);
        initialData[i].resize(sizeDist(gen));
        std::generate(initialData[i].begin(), initialData[i].end(), randomChar);
        configs[i].replica_num = replicaDist(gen);
    }

    // Initially put all objects
    std::vector<TaskID> initialVersions(numKeys);
    for (int i = 0; i < numKeys; ++i)
    {
        std::vector<Slice> putSlices = {{initialData[i].data(), initialData[i].size()}};
        initialVersions[i] = store.put(keys[i], putSlices, configs[i]);
        EXPECT_NE(initialVersions[i], 0);
    }

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    std::atomic<int> totalOperations{0};

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, &keys, &initialData, &configs, &initialVersions, &start, &gen, &totalOperations, &sizeDist, &replicaDist, numKeys, numOperationsPerThread]()
                             {
            std::uniform_int_distribution<> keyDist(0, numKeys - 1);
            std::uniform_int_distribution<> opDist(0, 3); // 0: put, 1: get, 2: remove, 3: replicate

            while (!start.load())
            {
                std::this_thread::yield();
            }

            for (int op = 0; op < numOperationsPerThread; ++op)
            {
                int keyIndex = keyDist(gen);
                int operation = opDist(gen);

                try
                {
                    switch (operation)
                    {
                    case 0: // put
                    {
                        std::vector<char> newData(sizeDist(gen));
                        std::generate(newData.begin(), newData.end(), randomChar);
                        std::vector<Slice> putSlices = {{newData.data(), newData.size()}};
                        TaskID version = store.put(keys[keyIndex], putSlices, configs[keyIndex]);
                        EXPECT_NE(version, 0);
                        break;
                    }
                    case 1: // get
                    {
                        std::vector<char> retrievedData(initialData[keyIndex].size());
                        std::vector<Slice> getSlices = {{retrievedData.data(), retrievedData.size()}};
                        TaskID getVersion = store.get(keys[keyIndex], getSlices, 0, 0); // Get latest version
                        if (getVersion < 0)
                        {
                            LOG(ERROR) << "get key: " << keys[keyIndex] << " , ret: " << errnoToString(getVersion);
                        }
                        break;
                    }
                    case 2: // remove
                    {
                        TaskID removeVersion = store.remove(keys[keyIndex]); // Remove latest version
                        if (removeVersion < 0)
                        {
                            LOG(ERROR) << "remove key: " << keys[keyIndex] << ", ret:" << errnoToString(removeVersion);
                        }
                        break;
                    }
                    case 3: // replicate
                    {
                        ReplicateConfig newConfig;
                        newConfig.replica_num = replicaDist(gen);
                        DistributedObjectStore::ReplicaDiff replicaDiff;
                        TaskID replicateVersion = store.replicate(keys[keyIndex], newConfig, replicaDiff);
                        if (replicateVersion < 0)
                        {
                            LOG(ERROR) << "replica key: " << keys[keyIndex] << ", ret: " << errnoToString(replicateVersion);
                        }
                        break;
                    }
                    }
                    totalOperations.fetch_add(1, std::memory_order_relaxed);
                }
                catch (const std::exception &e)
                {
                    LOG(ERROR) << "Exception in thread " << std::this_thread::get_id() << ": " << e.what();
                }
            } });
    }

    start.store(true);

    for (auto &thread : threads)
    {
        thread.join();
    }

    LOG(INFO) << "Total operations performed: " << totalOperations.load();

    // Final verification
    for (int i = 0; i < numKeys; ++i)
    {
        std::vector<char> finalData(initialData[i].size());
        std::vector<Slice> getSlices = {{finalData.data(), finalData.size()}};
        TaskID finalVersion = store.get(keys[i], getSlices, 0, 0); // Get latest version

        if (finalVersion >= 0)
        {
            LOG(INFO) << "Key " << keys[i] << " final version: " << finalVersion;
        }
        else
        {
            LOG(INFO) << "Key " << keys[i] << " not found (possibly removed)";
        }
    }
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging("test_log");
    google::SetLogDestination(google::INFO, "logs/log_info_");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}