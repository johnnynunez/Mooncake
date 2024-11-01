# P2P Store

P2P Store 基于 [Transfer Engine](transfer-engine.md) 构建，支持在集群中的对等节点之间临时共享对象，典型的场景包括 Checkpoint 分发等。P2P Store 是纯客户端架构，没有统一的 Master 节点，全局元数据由 etcd 服务维护。P2P Store 现已用于 Moonshot AI 的检查点传输服务。

P2P Store 提供的是类似 Register 和 GetReplica 的接口。Register 相当于 BT 中的做种将本地某个文件注册到全局元数据中去，此时并不会发生任何的数据传输，仅仅是登记一个元数据；GetReplica 接口会检索元数据，并从曾调用 Register 或 Get 的其他机器中克隆数据（除非显式调用 Unregister 或 DeleteReplica 停止从本地拉取文件），自身也可作为数据源提高其他节点传输数据的效率。这样做可以提高大规模数据分发效率，避免单机出口带宽饱和影响分发效率。

## P2P Store 演示程序
按照编译指南的说明，执行 `cmake .. -DWITH_P2P_STORE=ON && make -j` 编译 P2P Store 成功后，可在 `build/mooncake-p2p-store` 目录下产生测试程序 `p2p-store-example`。该工具演示了 P2P Store 的使用方法，模拟了训练节点完成训练任务后，将模型数据迁移到大量推理节点的过程。目前仅支持 RDMA 协议。

1. **启动 `etcd` 服务。** 这与 Transfer Engine Bench 所述的方法是一致的。
   
2. **启动模拟训练节点。** 该节点将创建模拟模型文件，并向集群内公开。
   ```bash
   # This is 10.0.0.2
   export MC_GID_INDEX=n
   ./p2p-store-example --cmd=trainer \
                       --metadata_server=10.0.0.1:2379 \
                       --local_server_name=10.0.0.2:12345 \
                       --device_name=erdma_0
   ```

3. **启动模拟推理节点。** 该节点会从模拟训练节点或其他模拟推理节点拉取数据。
   ```bash
   # This is 10.0.0.3
   export MC_GID_INDEX=n
   ./p2p-store-example --cmd=inferencer \
                       --metadata_server=10.0.0.1:2379 \
                       --local_server_name=10.0.0.3:12346 \
                       --device_name=erdma_1
   ```
   测试完毕显示“ALL DONE”。

上述过程中，模拟推理节点检索数据来源由 P2P Store 内部逻辑实现，因此不需要用户提供训练节点的 IP。同样地，需要保证其他节点可使用本机主机名 `hostname(2)` 或创建节点期间填充的 `--local_server_name` 来访问这台机器。
