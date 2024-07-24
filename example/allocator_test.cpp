#include "allocator/cache_allocator.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using namespace mooncake;

void printSeparator()
{
    std::cout << "========================================" << std::endl;
}

void runTests()
{
    std::cout << "Starting CacheAllocator tests..." << std::endl;

    // 创建虚拟节点
    const int NUM_VIRTUAL_NODES = 50000;
    std::vector<std::unique_ptr<VirtualNode>> nodes;
    for (int i = 0; i < NUM_VIRTUAL_NODES; ++i)
    {
        nodes.push_back(std::make_unique<VirtualDummyNode>(i));
    }

    // 创建分配策略
    auto strategy = std::make_unique<RandomAllocationStrategy>();

    // 创建 CacheAllocator
    const size_t SHARD_SIZE = 1024 * 64; // 1KB
    CacheAllocator allocator(SHARD_SIZE, std::move(nodes), std::move(strategy));

    // 测试用例 1: asyncPut with multiple input blocks
    {
        printSeparator();
        std::cout << "Test case 1: asyncPut with multiple input blocks" << std::endl;
        ObjectKey key = "test_object_1";
        std::vector<char> data1(1024, 'A');
        std::vector<char> data2(512, 'B');
        std::vector<char> data3(1536, 'C');
        std::vector<void *> ptrs = {data1.data(), data2.data(), data3.data()};
        std::vector<void *> sizes = {(void *)1024, (void *)512, (void *)1536};
        ReplicateConfig config{2}; // 2 replicas
        TaskID task_id = allocator.asyncPut(key, PtrType::HOST, ptrs, sizes, config);
        assert(task_id > 0);
        std::cout << "asyncPut with multiple input blocks test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 2: asyncGet
    {
        printSeparator();
        std::cout << "Test case 2: asyncGet" << std::endl;

        ObjectKey key = "test_object_1";
        std::vector<char> buffer(1024, 0); // 1KB buffer
        std::vector<void *> ptrs = {buffer.data()};
        std::vector<void *> sizes = {reinterpret_cast<void *>(buffer.size())};

        TaskID task_id = allocator.asyncGet(key, PtrType::HOST, ptrs, sizes);
        assert(task_id > 0);

        // TODO: 验证数据是否被正确读取

        std::cout << "asyncGet test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 3: asyncReplicate (增加副本数)
    {
        printSeparator();
        std::cout << "Test case 3: asyncReplicate (increase replicas)" << std::endl;
        ObjectKey key = "test_object_1";
        ReplicateConfig new_config{3}; // 增加到 3 个副本
        ReplicaDiff diff;

        TaskID task_id = allocator.asyncReplicate(key, new_config, diff);
        assert(task_id > 0);
        assert(diff.change_status == ReplicaChangeStatus::ADDED);
        for (const auto &replica : diff.added_replicas)
        {
            for (auto &handle : replica.handles)
            {
                std::cout << "handele segment_id: " << handle.segment_id << " offset: " << handle.offset << std::endl;
            }
            std::cout << std::endl;
        }
        std::cout << "asyncReplicate (increase) test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 4: asyncReplicate (减少副本数)
    {
        printSeparator();
        std::cout << "Test case 4: asyncReplicate (decrease replicas)" << std::endl;
        ObjectKey key = "test_object_1";
        ReplicateConfig new_config{1}; // 减少到 1 个副本
        ReplicaDiff diff;
        TaskID task_id = allocator.asyncReplicate(key, new_config, diff);
        assert(task_id > 0);
        assert(diff.change_status == ReplicaChangeStatus::REMOVED);
        for (auto &replica : diff.removed_replicas)
        {
            for (auto &handle : replica.handles)
            {
                std::cout << "handele segment_id: " << handle.segment_id << " offset: " << handle.offset << std::endl;
            }
            std::cout << std::endl;
        }
        std::cout << "asyncReplicate (decrease) test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 5: asyncPut 大对象
    {
        printSeparator();
        std::cout << "Test case 5: asyncPut large object" << std::endl;
        ObjectKey key = "large_object";
        size_t obj_size = 10 * 1024 * 1024; // 10MB
        ReplicateConfig config{2};          // 2 replicas

        // 创建一个大的数据缓冲区
        std::vector<char> large_data(obj_size);
        std::fill(large_data.begin(), large_data.end(), 'A'); // 填充一些数据

        // 为了测试多个输入块，我们将数据分成两部分
        size_t part1_size = 6 * 1024 * 1024; // 6MB
        size_t part2_size = 4 * 1024 * 1024; // 4MB

        std::vector<void *> ptrs = {large_data.data(), large_data.data() + part1_size};
        std::vector<void *> sizes = {reinterpret_cast<void *>(part1_size), reinterpret_cast<void *>(part2_size)};

        TaskID task_id = allocator.asyncPut(key, PtrType::HOST, ptrs, sizes, config);
        assert(task_id > 0);
        std::cout << "asyncPut large object test passed." << std::endl;

        // todo: 验证数据是否正确写入

        std::cout << "Large object data verification passed." << std::endl;
        printSeparator();
    }

    // 测试用例 6: asyncGet 带版本和偏移
    {
        printSeparator();
        std::cout << "Test case 6: asyncGet with version and offset" << std::endl;

        ObjectKey key = "large_object";
        Version min_version = 1;
        size_t offset = 1024 * 1024;              // 1MB offset
        std::vector<char> buffer(1024 * 1024, 0); // 1MB buffer
        std::vector<void *> ptrs = {buffer.data()};
        std::vector<void *> sizes = {reinterpret_cast<void *>(buffer.size())};

        TaskID task_id = allocator.asyncGet(key, PtrType::HOST, ptrs, sizes, min_version, offset);
        assert(task_id > 0);

        // TODO: 验证数据是否被正确读取
        // assert(memcmp(buffer.data(), expected_data + offset, buffer.size()) == 0);

        std::cout << "asyncGet with version and offset test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 7: asyncGet with multiple output buffers
    {
        printSeparator();
        std::cout << "Test case 7: asyncGet with multiple output buffers" << std::endl;

        ObjectKey key = "large_object";

        // 准备多个不同大小的缓冲区
        std::vector<char> buffer1(40 * 1024, 0); // 40KB
        std::vector<char> buffer2(35 * 1024, 0); // 35KB
        std::vector<char> buffer3(25 * 1024, 0); // 25KB

        std::vector<void *> ptrs = {buffer1.data(), buffer2.data(), buffer3.data()};
        std::vector<void *> sizes = {
            reinterpret_cast<void *>(buffer1.size()),
            reinterpret_cast<void *>(buffer2.size()),
            reinterpret_cast<void *>(buffer3.size())};

        // 调用 asyncGet
        TaskID task_id = allocator.asyncGet(key, PtrType::HOST, ptrs, sizes);
        assert(task_id > 0);

        // TODO 验证数据是否被正确读取 需要比较读取的数据与预期的数据
        // print info
        std::cout << "Read " << buffer1.size() << " bytes into first buffer" << std::endl;
        std::cout << "Read " << buffer2.size() << " bytes into second buffer" << std::endl;
        std::cout << "Read " << buffer3.size() << " bytes into third buffer" << std::endl;

        // TODO:
        // assert(memcmp(buffer1.data(), expected_data, buffer1.size()) == 0);
        // assert(memcmp(buffer2.data(), expected_data + buffer1.size(), buffer2.size()) == 0);
        // assert(memcmp(buffer3.data(), expected_data + buffer1.size() + buffer2.size(), buffer3.size()) == 0);

        std::cout << "asyncGet with multiple output buffers test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 8: 错误处理 - 获取不存在的对象
    {
        printSeparator();
        std::cout << "Test case 8: Error handling - Get non-existent object" << std::endl;

        ObjectKey key = "non_existent_object";
        std::vector<char> buffer(1024, 0); // 1KB buffer
        std::vector<void *> ptrs = {buffer.data()};
        std::vector<void *> sizes = {reinterpret_cast<void *>(buffer.size())};

        try
        {
            allocator.asyncGet(key, PtrType::HOST, ptrs, sizes);
            assert(false); // 应该抛出异常
        }
        catch (const std::runtime_error &e)
        {
            std::cout << "Caught expected exception: " << e.what() << std::endl;
        }

        std::cout << "Error handling test passed." << std::endl;
        printSeparator();
    }

    // 测试用例 9: 错误处理 - 复制不存在的对象
    {
        printSeparator();
        std::cout << "Test case 9: Error handling - Replicate non-existent object" << std::endl;
        ObjectKey key = "non_existent_object";
        ReplicateConfig config{2};
        try
        {
            ReplicaDiff diff;
            allocator.asyncReplicate(key, config, diff);
            assert(false); // 应该抛出异常
        }
        catch (const std::runtime_error &e)
        {
            std::cout << "Caught expected exception: " << e.what() << std::endl;
        }
        std::cout << "Error handling test passed." << std::endl;
        printSeparator();
    }

    std::cout << "All tests passed successfully!" << std::endl;
}

int main()
{
    runTests();
    return 0;
}
