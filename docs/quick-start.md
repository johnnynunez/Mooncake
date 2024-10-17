# 快速使用指南

Mooncake 由 Transfer Engine、Managed Store、P2P Store 等组件组成，这些组件可按应用需求分别进行编译和使用。

## 编译
Mooncake 目前仅支持 Linux 操作系统，并且依赖以下软件：
- Go 1.20+
- GCC 9.4+
- CMake 3.16+
- etcd 3.2+

1. 克隆源码仓库
   ```bash
   git clone https://github.com/kvcache-ai/mooncake-dev.git

   ```
2. 进入源码目录
   ```bash
   cd mooncake
   ```

3. 切换到目标分支，如 `v0.1`
   ```bash
   git checkout v0.1
   ```

   > The development branch often involves large changes, so do not use the clients compiled in the "development branch" for the production environment.

5. 安装依赖库（需要 root 权限）
   ```bash
   bash dependencies.sh
   ```

6. 编译 Mooncake 组件
   ```bash
   mkdir build
   cd build
   cmake ..
   make -j
   ```

### 高级编译选项
Mooncake 支持在执行 `cmake` 命令期间添加下列高级编译选项：
- `-DUSE_CUDA=[ON|OFF]`：编译 Transfer Engine 时启用或关闭 GPU Direct RDMA 功能的支持。仅支持 NVIDIA CUDA，需要事先安装相应的依赖库。（不包含在 `dependencies.sh` 脚本中）。默认关闭。
- `-DUSE_CXL=[ON|OFF]`：编译 Transfer Engine 时启用或关闭 CXL 协议的支持。默认关闭。
- `-DWITH_P2P_STORE=[ON|OFF]`：编译 P2P Store 及示例程序，默认开启。
- `-DWITH_ALLOCATOR=[ON|OFF]`：编译 Managed Store 所用的中心分配器模块，默认开启。

## Transfer Engine Bench 使用方法
按照上面步骤编译 Transfer Engine 成功后，可在 `build/mooncake-transfer-engine/example` 目录下产生测试程序 `transfer_engine_bench`。该工具通过调用 Transfer Engine 接口，发起节点从目标节点的 DRAM 处反复读取/写入数据块，以展示 Transfer Engine 的基本用法，并可用于测量读写吞吐率。目前 Transfer Engine Bench 工具仅用于 RDMA 协议。

1. **启动 `etcd` 服务。** 该服务用于 Mooncake 各类元数据的集中高可用管理，包括 Transfer Engine 的内部连接状态等。需确保发起节点和目标节点都能顺利通达该 etcd 服务，因此需要注意：
   - etcd 服务的监听 IP 不应为 127.0.0.1，需结合网络环境确定。在实验环境中，可使用 0.0.0.0。例如，可使用下列命令行启动合要求的服务：
      ```bash
      ./etcd --listen-client-urls http://0.0.0.0:2379 --advertise-client-urls http://<your-server-ip>:2379
      ```
   - 在某些平台下，如果发起节点和目标节点设置了 `http_proxy` 或 `https_proxy` 环境变量，也会影响 Transfer Engine 与 etcd 服务的通信，报告“Error from etcd client: 14”错误。

2. **启动目标节点。**
    ```bash
    ./transfer_engine_test --mode=target 
                           --metadata_server=etcd_server_ip:2379
                           [--local_server_name=server_name[:port]]
                           [--nic_priority_matrix=/path/to/json]
    ```
   各个参数的含义如下：
   - `--mode=target` 表示启动目标节点。目标节点不发起读写请求，只是被动按发起节点的要求供给或写入数据。
      > 注意：实际应用中可不区分目标节点和发起节点，每个节点可以向集群内其他节点自由发起读写请求。
   - `--metadata_server` 为元数据服务器地址（etcd 服务的完整地址），如 `etcd_server_name:2379`。
   - `--local_server_name` 表示本机器地址，大多数情况下无需设置。如果不设置该选项，则优先使用本机的主机名（即 `hostname(2)` ）并占用默认的带外通信 RPC 端口。集群内的其它节点会使用此地址尝试与该节点进行带外通信，从而建立 RDMA 连接。
      > 注意：若带外通信失败则连接无法建立。因此，若有必要需修改集群所有节点的 `/etc/hosts` 文件，使得可以通过主机名定位到正确的节点。
   - `--nic_priority_matrix` 表示网卡优先级矩阵。可传入网卡优先级矩阵字符串，或内容为上述字符串的文本文件路径。在实验环境中，网卡优先级矩阵的最简单格式如下：
        ```json
        {  "cpu:0": [["mlx5_0"], []] }
        ```
      只需要结合自身的网络环境，将 `"mlx5_0"` 替换成实际使用的网卡名称即可。

3. **启动发起节点。**
    ```bash
    ./transfer_engine_test --metadata_server=etcd_server_ip:2379
                           --segment_id=target_server_name[:port]
                           [--nic_priority_matrix=/path/to/json]
    ```
   各个参数的含义如下：
   - `--segment_id` 可以简单理解为目标节点的主机名，需要和启动目标节点时 `--local_server_name` 传入的值（如果有）保持一致。
   
   正常情况下，发起节点将开始进行传输操作，等待 10s 后回显“Test completed”信息，表明测试完成。

   发起节点还可以配置下列测试参数：`--operation`（可为 `"read"` 或 `"write"`）、`batch_size`、`block_size`、`duration`、`threads` 等。



## P2P Store 使用与测试方法
TBD