# Mooncake

## 编译

1. 通过系统源下载安装下列第三方库
   ```bash
   apt-get install -y build-essential \
                      cmake \
                      libibverbs-dev \
                      libgoogle-glog-dev \
                      libjsoncpp-dev \
                      libnuma-dev
   ```

2. 安装 etcd-cpp-apiv3 库（https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3），请参阅 `Build and install` 一节的说明，并确保 `make install` 执行成功

3. 进入项目根目录，运行
   ```
   mkdir build; cd build; cmake ..; make -j
   ```

正常情况下编译应当成功，如果编译失败请检查上述第三方库是否已经安装，以及 `make install` 是否已经执行成功。将在 `build` 目录下生成静态库文件 `build/src/transfer_engine/libtransfer_engine.a`，与测试用程序 `build/example/transfer_engine_test`。

## 静态库文件的使用
要利用 TransferEngine 进行二次开发，可
- 将 `src/transfer_engine` 目录下的文件整体移入项目目录，或者
- 使用编译好的静态库文件 `build/src/transfer_engine/libtransfer_engine.a` 及 C 头文件 `src/transfer_engine/transfer_engine_c.h`，不需要用到 `src/transfer_engine` 下的其他文件

如果要实现与 Go 语言代码的结合，请参考 `checkpoint` 目录下的样例，特别是 `checkpoint/transfer_engine.go`
```
package main

//#cgo LDFLAGS: -L../build/src/transfer_engine -ltransfer_engine -lstdc++ -lnuma -lglog -libverbs -ljsoncpp -letcd-cpp-api
//#include "../src/transfer_engine/transfer_engine_c.h"
import "C"

.// ...
```
请按实际情况修改 `-L../build/src/transfer_engine`（静态库文件路径）及 `#include "../src/transfer_engine/transfer_engine_c.h"`（头文件）。LDFLAGS 的其他选项不建议调整。

## 样例程序使用
样例程序的代码见 `example/transfer_engine_test.cpp`。使用要点如下:

1. 在机器 E 上启动 etcd 服务，记录该服务的 [IP/域名:端口]，如 optane21:2379。要求集群内所有服务器能通过该 [IP/域名:端口] 访问到 etcd 服务（通过 curl 验证），因此若有必要需要设置集群所有节点的 /etc/hosts 文件，或者换用 IP 地址。

    使用命令行直接启动 etcd，需确保设置 --listen-client-urls 参数为 0.0.0.0：
    ```
    bash
    ./etcd --listen-client-urls http://0.0.0.0:2379 --advertise-client-urls http://<your-server-ip>:2379
    ```

2. 在机器 A 上启动 transfer_engine 服务，模式设置为 target，负责在测试中提供 Segment 源（实际环境中每个节点既可发出传输请求，也可作为传输数据源）

    ```
    ./example/transfer_engine_test --mode=target <通用选项>
    ```
   
    为了正确建立连接，一般需要设置下列通用选项：
    - metadata_server：为开启 etcd 服务机器 E 的 [IP/域名:端口]，如 optane21:12345。
    - local_server_name：本机器 [IP/域名]，如 optane20，如果不设置，则直接使用本机的主机名。本机内存形成的 Segment 名称与 local_server_name 一致。其他节点会直接使用 local_server_name（可为 IP 或域名形态）与本机进行 RDMA EndPoint 握手，若握手失败则无法完成后续通信。因此，务必需要保证填入的参数是有效的 IP 或域名，若有必要需要设置集群所有节点的 /etc/hosts 文件。
    - nic_priority_matrix：网卡优先级矩阵。一种最简单的形态如下：
        ```
        {  "cpu:0": [["mlx5_1", "mlx5_2", "mlx5_3", "mlx5_4"], []] }
        ```
        表示，对于登记类别（即 registerMemory 的 location 字段）为 "cpu:0" 的内存区域，优先从 "mlx5_1", "mlx5_2", "mlx5_3", "mlx5_4" 中随机选取一张网卡建立连接并传输。

3. 在机器 B 上再启动一个 transfer_engine 服务，用以发起 transfer 请求。segment_id 表示测试用 Segment 来源，在这里就是机器 A 的 [IP/域名]。
    ```
    ./example/transfer_engine_test --segment_id=[IP/域名] <通用选项>
    ```
    此外，operation（可为 read 或 write）、batch_size、block_size、duration、threads 等均为测试配置项，其含义不言自明。

