#include "buffer_allocator.h"
#include "random_allocation_strategy.h"
#include "replica_allocator.h"
#include <iostream>
#include <memory>
#include <vector>

using namespace mooncake;

void testAddOneReplica(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "-----------------testAddOneReplica begin----------------------\n";
    ObjectKey key = "test_object";
    ReplicaInfo info;
    Version version = allocator->addOneReplica(key, info, DEFAULT_VALUE, 2048); // 2KB object
    LOG(INFO) << "testAddOneReplica, the version: " << version;
    if (info.handles.size() == 2 && info.handles[0]->size == 1024 && info.handles[1]->size == 1024)
    {
        std::cout << "-----------------testAddOneReplica passed----------------------\n";
    }
    else
    {
        std::cout << "------------------testAddOneReplica failed-------------------------\n";
    }
}

void testAddOneReplicaWithStrategy(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "-----------------testAddOneReplicaWithStrategy begin----------------------\n";
    ObjectKey key = "test_object";
    ReplicaInfo info;
    auto config = std::make_shared<RandomAllocationStrategyConfig>();
    std::shared_ptr<AllocationStrategy> strategy = std::make_shared<RandomAllocationStrategy>(config);
    Version version = allocator->addOneReplica(key, info, DEFAULT_VALUE, 2048, strategy); // 2KB object
    LOG(INFO) << "testAddOneReplicaWithStrategy, the version: " << version;
    if (info.handles.size() == 2 && info.handles[0]->size == 1024 && info.handles[1]->size == 1024)
    {
        std::cout << "-----------------testAddOneReplicaWithStrategy passed----------------------\n";
    }
    else
    {
        std::cout << "------------------testAddOneReplicaWithStrategy failed-------------------------\n";
    }
}

void testAddMultipleReplicas(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "----------------------------testAddMultipleReplicas begin---------------------------\n";

    ObjectKey key = "multi_replica_object";
    ReplicaInfo info1, info2, info3;

    Version v1 = allocator->addOneReplica(key, info1, DEFAULT_VALUE, 3072); // 3KB object
    Version v2 = allocator->addOneReplica(key, info2, v1);
    Version v3 = allocator->addOneReplica(key, info3, v1);
    LOG(INFO) << "testAddMultipleReplicas, the version: " << v1 << " " << v2 << " " << v3;
    if (v1 == v2 && v2 == v3 && info1.handles.size() == 3 && info2.handles.size() == 3 && info3.handles.size() == 3)
    {
        std::cout << "----------------------------testAddMultipleReplicas passed---------------------------\n";
    }
    else
    {
        std::cout << "-------------------------testAddMultipleReplicas failed--------------------------\n";
    }
    ReplicaInfo del;
    allocator->removeOneReplica(key, del, v1);
}

void testReassignReplica(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "----------------------------testReassignReplica begin---------------------------\n";

    ObjectKey key = "reassign_object";
    ReplicaInfo info;
    Version version = allocator->addOneReplica(key, info, DEFAULT_VALUE, 2048);
    LOG(INFO) << "testReassignReplica, the version: " << version;
    // Simulate a shard becoming partial
    info.handles[0]->status = BufStatus::FAILED;
    info.handles[1]->status = BufStatus::COMPLETE;

    ReplicaInfo new_info;
    allocator->reassignReplica(key, version, 0, new_info);

    if (new_info.handles.size() == 2 && new_info.handles[0] != info.handles[0] &&
        new_info.handles[1] == info.handles[1])
    {
        std::cout << "---------------------------testReassignReplica passed-----------------------\n";
    }
    else
    {
        std::cout << "---------------------testReassignReplica failed--------------------------\n";
    }
}

void testRemoveOneReplica(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "-------------------------testRemoveOneReplica begin-------------------------\n";
    ObjectKey key = "remove_object";
    ReplicaInfo info1, info2, removed_info;

    Version version = allocator->addOneReplica(key, info1, DEFAULT_VALUE, 1536); // 1.5KB object
    allocator->addOneReplica(key, info2, version);

    allocator->removeOneReplica(key, removed_info, version);

    if (removed_info.handles.size() == 2 && removed_info.handles[0]->size == 1024 &&
        removed_info.handles[1]->size == 512)
    {
        std::cout << "-------------------------testRemoveOneReplica passed-------------------------\n";
    }
    else
    {
        std::cout << "-----------------------testRemoveOneReplica failed-------------------------\n";
    }
}

void testLargeObjectAllocation(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "-----------------------testLargeObjectAllocation begin----------------------\n";
    ObjectKey key = "large_object";
    ReplicaInfo info;
    Version version = allocator->addOneReplica(key, info, DEFAULT_VALUE, 20480); // 20KB object
    LOG(INFO) << "testLargeObjectAllocation, the version: " << version;
    bool passed = (info.handles.size() == 20);
    for (const auto &handle : info.handles)
    {
        if (handle->size != 1024)
        {
            passed = false;
            break;
        }
    }

    if (passed)
    {
        std::cout << "-----------------------testLargeObjectAllocation passed----------------------\n";
    }
    else
    {
        std::cout << "---------------------testLargeObjectAllocation failed-----------------------\n";
    }
}

void testMultipleVersions(std::shared_ptr<ReplicaAllocator> &allocator)
{
    std::cout << "-------------------testMultipleVersions begin----------------------------\n";
    ObjectKey key = "versioned_object";
    ReplicaInfo info1, info2;

    Version v1 = allocator->addOneReplica(key, info1, DEFAULT_VALUE, 1024);
    Version v2 = allocator->addOneReplica(key, info2, DEFAULT_VALUE, 2048);
    LOG(INFO) << "testMultipleVersions, the version: " << v1 << " " << v2;
    if (info1.handles.size() == 1 && info2.handles.size() == 2)
    {
        std::cout << "-------------------testMultipleVersions passed----------------------------\n";
    }
    else
    {
        std::cout << "------------------testMultipleVersions failed--------------------------\n";
    }
}

void testRecovery(std::shared_ptr<ReplicaAllocator> &allocator, SegmentId segment, uint64_t allocator_index)
{
    std::vector<std::shared_ptr<BufHandle>> handles = allocator->unregister(segment, allocator_index);
    allocator->recovery(handles);
}

void runAllTests(std::shared_ptr<ReplicaAllocator> allocator)
{
    testAddOneReplica(allocator);
    testAddOneReplicaWithStrategy(allocator);
    testAddMultipleReplicas(allocator);
    testReassignReplica(allocator);
    testRemoveOneReplica(allocator);
    testLargeObjectAllocation(allocator);
    testMultipleVersions(allocator);
}

void concurrentTest(std::shared_ptr<ReplicaAllocator> allocator, int threadId, std::atomic<bool> &stop)
{
    // while (!stop.load(std::memory_order_relaxed)) {
    try
    {
        runAllTests(allocator);
        std::cout << "Thread " << threadId << " completed one round of tests." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Thread " << threadId << ", stop: " << stop << " encountered an error: " << e.what() << std::endl;
    }
    //}
}

int main(int argc, char *argv[])
{
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::INFO, "logs/log_info_");
    // Initialize ReplicaAllocator with shard size of 1024 bytes
    auto allocator = std::make_shared<ReplicaAllocator>(1024);

    // Register some buffers
    size_t base = 0x100000000;
    size_t size = 1024 * 1024 * 4;
    uint64_t allocator_index = allocator->registerBuffer(1, base, size * 2);
    allocator->registerBuffer(2, base + size * 2, size * 2);
    allocator->registerBuffer(3, base + size * 4, size * 4);

    // Number of concurrent threads
    const int numThreads = 4;
    std::vector<std::thread> threads;
    std::atomic<bool> stop(false);

    // Start concurrent test threads
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(concurrentTest, allocator, i, std::ref(stop));
    }

    // std::this_thread::sleep_for(std::chrono::seconds(1));

    // Signal threads to stop
    stop.store(true, std::memory_order_relaxed);

    // Wait for all threads to finish
    for (auto &thread : threads)
    {
        thread.join();
    }
    testRecovery(allocator, 1, allocator_index);
    return 0;
}
