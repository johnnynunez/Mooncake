# 范例程序使用指南

## Transfer Engine Bench 使用方法
编译 Transfer Engine 成功后，可在 `build/mooncake-transfer-engine/example` 目录下产生测试程序 `transfer_engine_bench`。该工具通过调用 Transfer Engine 接口，发起节点从目标节点的 DRAM 处反复读取/写入数据块，以展示 Transfer Engine 的基本用法，并可用于测量读写吞吐率。目前 Transfer Engine Bench 工具可用于 RDMA 协议（GPUDirect 正在测试） 及 TCP 协议。

1. **启动 `etcd` 服务。** 该服务用于 Mooncake 各类元数据的集中高可用管理，包括 Transfer Engine 的内部连接状态等。需确保发起节点和目标节点都能顺利通达该 etcd 服务，因此需要注意：
   - etcd 服务的监听 IP 不应为 127.0.0.1，需结合网络环境确定。在实验环境中，可使用 0.0.0.0。例如，可使用下列命令行启动合要求的服务：
      ```bash
      # This is 10.0.0.1
      etcd --listen-client-urls http://0.0.0.0:2379 --advertise-client-urls http://10.0.0.1:2379
      ```
   - 在某些平台下，如果发起节点和目标节点设置了 `http_proxy` 或 `https_proxy` 环境变量，也会影响 Transfer Engine 与 etcd 服务的通信，报告“Error from etcd client: 14”错误。

2. **启动目标节点。**
    ```bash
    # This is 10.0.0.2
    export MC_GID_INDEX=n
    ./transfer_engine_bench --mode=target \
                            --metadata_server=10.0.0.1:2379 \
                            --local_server_name=10.0.0.2:12345 \
                            --device_name=erdma_0
    ```
   各个参数的含义如下：
   - 环境变量 `MC_GID_INDEX` 的默认值为 3，是大多数 IB/RoCE 网络所用的 GID Index。对于阿里 eRDMA，需要设置为 1。
   - `--mode=target` 表示启动目标节点。目标节点不发起读写请求，只是被动按发起节点的要求供给或写入数据。
      > 注意：实际应用中可不区分目标节点和发起节点，每个节点可以向集群内其他节点自由发起读写请求。
   - `--metadata_server` 为元数据服务器地址（etcd 服务的完整地址）。
   - `--local_server_name` 表示本机器地址，大多数情况下无需设置。如果不设置该选项，则该值等同于本机的主机名（即 `hostname(2)` ）。集群内的其它节点会使用此地址尝试与该节点进行带外通信，从而建立 RDMA 连接。
      > 注意：若带外通信失败则连接无法建立。因此，若有必要需修改集群所有节点的 `/etc/hosts` 文件，使得可以通过主机名定位到正确的节点。
   - `--device_name` 表示传输过程使用的 RDMA 网卡名称。
      > 提示：高级用户还可通过 `--nic_priority_matrix` 传入网卡优先级矩阵 JSON 文件，详细参考 Transfer Engine 的开发者手册。
   - 在仅支持 TCP 的网络环境中，可使用 `--protocol=tcp` 参数，此时不需要指定 `--device_name` 参数。

1. **启动发起节点。**
    ```bash
    # This is 10.0.0.3
    export MC_GID_INDEX=n
    ./transfer_engine_bench --metadata_server=10.0.0.1:2379 \
                            --segment_id=10.0.0.2:12345 \
                            --local_server_name=10.0.0.3:12346 \
                            --device_name=erdma_1
    ```
   各个参数的含义如下（其余同前）：
   - `--segment_id` 可以简单理解为目标节点的主机名，需要和启动目标节点时 `--local_server_name` 传入的值（如果有）保持一致。
   
   正常情况下，发起节点将开始进行传输操作，等待 10s 后回显“Test completed”信息，表明测试完成。

   发起节点还可以配置下列测试参数：`--operation`（可为 `"read"` 或 `"write"`）、`batch_size`、`block_size`、`duration`、`threads` 等。



## P2P Store 使用与测试方法
按照上面步骤编译 P2P Store 成功后，可在 `build/mooncake-p2p-store` 目录下产生测试程序 `p2p-store-example`。该工具演示了 P2P Store 的使用方法，模拟了训练节点完成训练任务后，将模型数据迁移到大量推理节点的过程。目前仅支持 RDMA 协议。

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
