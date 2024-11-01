# Transfer Engine Bench
`mooncake-transfer-engine/example/transfer_engine_bench.cpp` 提供了一个样例程序，通过调用 Transfer Engine 接口，发起节点从目标节点的 DRAM 处反复读取/写入数据块，以展示 Transfer Engine 的基本用法，并可用于测量读写吞吐率。目前 Transfer Engine Bench 工具可用于 RDMA 及 TCP 协议。

编译 Transfer Engine 成功后，可在 `build/mooncake-transfer-engine/example` 目录下产生测试程序 `transfer_engine_bench`。

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

> 如果在执行期间发生异常，大多数情况是参数设置不正确所致，建议参考[故障排除文档](troubleshooting.md)先行排查。