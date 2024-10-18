# Allocator Python 接口演示程序用法 [WIP]

一个最简单的测试环境是：一个 Memory Pool 进程、一个 Python 工作进程、一个 etcd 进程。三个进程都在同一台主机上运行。网卡有 erdma_0 和 erdma_1。

1. **启动 `etcd` 服务。** 

2. **启动 Memory Pool 进程。** `memory_pool` 在 `mooncake-transfer-engine/example` 目录下。
   ```bash
   ./memory_pool --metadata_server=localhost:2379 \
                 --local_server_name=localhost:12458 \
                 --device_name=erdma_0
   ```

3. **启动工作进程。** 按需完成下列操作：
   - 进入 `mooncake-allocator/src` 目录，该目录应当有一个 `distributed_object_store.cpython-310-x86_64-linux-gnu.so` 文件
   - 在该目录下写入 `conf/allocator.conf` 文件：
   ```
   local_server_name=localhost:10087
   metadata_server=localhost:2379
   nic_priority_matrix={"cpu:0":[["erdma_0"],[]]}
   ```
   - 执行 Python 程序，内容如下：
   ```python
   import distributed_object_store as mooncake

   store = mooncake.DistributedObjectStore()

   # 预备阶段，需要注册可用的所有 Segment：
   segment_name = "localhost:12458" # 可能要修改，需要和 Memory Pool 进程的 local_server_name 一致
   ret = store.register_buffer(segment_name, 0x40000000000, 1024 * 1024 * 1024)
   if ret < 0:
      exit(1)

   sample_text = "hello world"
   ret = store.put_object("foo", sample_text)
   if ret < 0:
      exit(1)

   output = store.get_object("foo", 12, 0, 0)
   if len(output == 0):
      exit(1)

   print("Output: ", output)
   ```
   TBD 目前 put_object/get_object 的 value 都是字符串类型，不知道 Pybind 怎么实现 bytes 的传递。这个只需要改 `mooncake-allocator/src/distributed_object_store_helper.cpp` 即可