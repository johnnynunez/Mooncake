# Transfer Engine C/C++ API

**[Transfer Engine](doc/zh/architecture.md#transfer-engine) 是一个围绕 Segment 和 BatchTransfer 两个核心抽象设计的高性能，零拷贝数据传输库。**

Transfer Engine 通过 `TransferEngine` 类对外提供接口（位于 `mooncake-transfer-engine/include/transfer_engine.h`），其中对应不同后端的具体的数据传输功能由 `Transport` 类实现，目前支持 `RdmaTransport` 和 `NVMeoFTransport`。

## 数据传输

### Transport::TransferRequest

Mooncake Transfer Engine 提供的最核心 API 是：通过 `Transport::submitTransfer` 接口提交一组异步的 `Transport::TransferRequest` 任务，并通过 `Transport::getTransferStatus` 接口查询其状态。每个 `Transport::TransferRequest` 规定从本地的起始地址 `source` 开始，读取或写入长度为 `length` 的连续数据空间，到 `target_id` 对应的段、从 `target_offset` 开始的位置。

`Transport::TransferRequest` 结构体定义如下：

```cpp
using SegmentID = int32_t;
struct TransferRequest
{
    enum OpCode { READ, WRITE };
    OpCode opcode;
    void *source;
    SegmentID target_id; // 目标 segment 的 ID，可能对应本地或远程的 DRAM/VRAM/NVMeof，具体的选路逻辑被隐藏
    size_t target_offset;
    size_t length;
};
```

- `opcode` 取值为 `READ` 或 `WRITE`。`READ` 表示数据从 `<target_id, target_offset>` 表示的目标地址复制到本地的起始地址 `source`；`WRITE` 表示数据从 `source` 复制到 `<target_id, target_offset>` 表示的地址。
- `source` 表示当前 `TransferEngine` 管理的 DRAM/VRAM buffer，需提前已经被 `registerLocalMemory` 接口注册
- `target_id` 表示传输目标的 Segment ID。Segment ID 的获取需要用到 `openSegment` 接口。Segment 分为以下两种类型：
  - RAM 空间型，涵盖 DRAM/VRAM 两种形态。如前所述，同一进程（或者说是 `TransferEngine` 实例）下只有一个 Segment，这个 Segment 内含多种不同种类的 Buffer（DRAM/VRAM）。此时 `openSegment` 接口传入的 Segment 名称等同于服务器主机名。`target_offset` 为目标服务器的虚拟地址。
  - NVMeOF 空间型，每个文件对应一个 Segment。此时 `openSegment` 接口传入的 Segment 名称等同于文件的唯一标识符。`target_offset` 为目标文件的偏移量。
- `length` 表示传输的数据量。TransferEngine 在内部可能会进一步拆分成多个读写请求。

### Transport::allocateBatchID

```cpp
BatchID allocateBatchID(size_t batch_size);
```

分配 `BatchID`。同一 `BatchID` 下最多可提交 `batch_size` 个 `TransferRequest`。

- `batch_size`: 同一 `BatchID` 下最多可提交的 `TransferRequest` 数量；
- 返回值：若成功，返回 `BatchID`（非负）；否则返回负数值。

### Transport::submitTransfer

```cpp
int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries);
```

向 `batch_id` 追加提交新的 `TransferRequest` 任务。该任务被异步提交到后台线程池。同一 `batch_id` 下累计的 `entries` 数量不应超过创建时定义的 `batch_size`。

- `batch_id`: 所属的 `BatchID`；
- `entries`: `TransferRequest` 数组；
- 返回值：若成功，返回 0；否则返回负数值。

### Transport::getTransferStatus

```cpp
enum TaskStatus
{
  WAITING,   // 正在处于传输阶段
  PENDING,   // 暂不支持
  INVALID,   // 参数不合法
  CANNELED,  // 暂不支持
  COMPLETED, // 传输完毕
  TIMEOUT,   // 暂不支持
  FAILED     // 即使经过重试仍传输失败
};
struct TransferStatus {
  TaskStatus s;
  size_t transferred; // 已成功传输了多少数据（不一定是准确值，确保是 lower bound）
};
int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
```

获取 `batch_id` 中第 `task_id` 个 `TransferRequest` 的运行状态。

- `batch_id`: 所属的 `BatchID`；
- `task_id`: 要查询的 `TransferRequest` 序号；
- `status`: 输出 Transfer 状态；
- 返回值：若成功，返回 0；否则返回负数值。

### Transport::freeBatchID

```cpp
int freeBatchID(BatchID batch_id);
```

回收 `BatchID`，之后对此的 `submitTransfer` 及 `getTransferStatus` 操作均是未定义的。若 `BatchID` 内仍有 `TransferRequest` 未完成，则拒绝操作。

- `batch_id`: 所属的 `BatchID`；
- 返回值：若成功，返回 0；否则返回负数值。

## 多 Transport 管理
`TransferEngine` 类内部管理多后端的 `Transport` 类，用户可向 `TransferEngine` 中装载或卸载对不同后端进行传输的 `Transport`。

### TransferEngine::installOrGetTransport
```cpp
Transport* installOrGetTransport(const std::string& proto, void** args);
```
在 `TransferEngine` 中注册 `Transport`。如果某个协议对应的 `Transport` 已存在，则返回该 `Transport`。

- `proto`: `Transport` 使用的传输协议名称，目前支持 `rdma`, `nvmeof`。
- `args`：以变长数组形式呈现的 `Transport` 初始化需要的其他参数，数组内最后一个成员应当是 `nullptr`。
- 返回值：若 `proto` 在确定范围内，返回对应 `proto` 的 `Transport`；否则返回空指针。

#### RDMA 传输模式
对于 RDMA 传输模式，注册 `Transport` 期间需通过 `args` 指定网卡优先级顺序。
```cpp
void** args = (void**) malloc(2 * sizeof(void*));
args[0] = /* topology matrix */;
args[1] = nullptr;
engine->installOrGetTransport("rdma", args);
```
网卡优先级顺序是一个 JSON 字符串，表示使用的存储介质名称及优先使用的网卡列表，样例如下：
```json
{
    "cpu:0": [["mlx0", "mlx1"], ["mlx2", "mlx3"]],
    "cuda:0": [["mlx1", "mlx0"]],
    ...
}
```
其中每个 `key` 代表一个 CPU socket 或者一个 GPU device 对应的设备名称
每个 `value` 为一个 (`preferred_nic_list`, `accessable_nic_list`) 的二元组，每一项都是一个 NIC 名称的列表（list）。
- `preferred_nic_list` 表示优先选择的 NIC，比如对于 CPU 可以是当前直连而非跨 NUMA 的 NIC，对于 GPU 可以是挂在同一个 PCIe Switch 下的 NIC；
- `accessable_nic_list` 表示虽然不优选但是理论上可以连接上的 NIC，用于故障重试场景。

#### NVMeOF 传输模式
对于 NVMeOF 传输模式，注册 `Transport` 期间需通过 `args` 指定文件路径。
```cpp
void** args = (void**) malloc(2 * sizeof(void*));
args[0] = /* topology matrix */;
args[1] = nullptr;
engine->installOrGetTransport("nvmeof", args);
```

### TransferEngine::uinstallTransport
```cpp
int uninstallTransport(const std::string& proto);
```
从 `TransferEngine` 中卸载 `Transport`。
- `proto`: `Transport` 使用的传输协议名称，目前支持 `rdma`, `nvmeof`。
- 返回值：若成功，返回 0；否则返回负数值。

## 空间注册

对于 RDMA 的传输过程，作为源端指针的 `TransferRequest::source` 必须提前注册为 RDMA 可读写的 Memory Region 空间，即纳入当前进程中 RAM Segment 的一部分。因此需要用到如下函数：

### TransferEngine::registerLocalMemory

```cpp
int registerLocalMemory(void *addr, size_t size, string location, bool remote_accessible);
```

在本地 DRAM/VRAM 上注册起始地址为 `addr`，长度为 `size` 的空间。

- `addr`: 注册空间起始地址；
- `size`：注册空间长度；
- `location`: 这一段内存对应的 `device`，比如 `cuda:0` 表示对应 GPU 设备，`cpu:0` 表示对应 CPU socket，通过和网卡优先级顺序表（见`installOrGetTransport`） 匹配，识别优选的网卡。
- `remote_accessible`: 标识这一块内存能否被远端节点访问。
- 返回值：若成功，返回 0；否则返回负数值。

### TransferEngine::unregisterLocalMemory

```cpp
int unregisterLocalMemory(void *addr);
```

解注册区域。

- addr: 注册空间起始地址；
- 返回值：若成功，返回 0；否则返回负数值。

## Segment 管理与 etcd 元数据

TranferEngine 提供 `openSegment` 函数，该函数获取一个 `SegmentHandle`，用于后续 `Transport` 的传输。
```cpp
SegmentHandle openSegment(const std::string& segment_name);
```
- `segment_name`：segment 的唯一标志符。对于 RAM Segment，这需要与对端进程初始化 TransferEngine 对象时填写的 `server_name` 保持一致。
- 返回值：若成功，返回对应的 SegmentHandle；否则返回负数值。
  
```cpp
int closeSegment(SegmentHandle segment_id);
```
- `segment_id`：segment 的唯一标志符。
- 返回值：若成功，返回 0；否则返回负数值。

<details>
<summary><strong>etcd 元数据形态</strong></summary>

```
// 用于根据 server_name 查找可通信的地址以及暴露的 rpc 端口。
// 创建：调用 TransferEngine::init() 时。
// 删除：TransferEngine 被析构时。
Key = mooncake/rpc_meta/[server_name]
Value = {
    'ip_or_host_name': 'node01'
    'rpc_port': 12345
}

// 对于 segment，采用 mooncake/[proto]/[segment_name] 的 key 命名方式，segment name 可以采用 Server Name。 
// Segment 对应机器，buffer 对应机器内的不同段内存或者不同的文件或者不同的盘。同一个 segment 的不同 buffer 处于同一个故障域。

// RAM Segment，用于 RDMA Transport 获取传输信息。
// 创建：命令行工具 register.py，此时 buffers 为空，仅填入可预先获知的信息。
// 修改：TransferEngine 在运行时通过 register / unregister 添加或删除 Buffer。
Key = mooncake/ram/[segment_name]
Value = {
    'server_name': server_name,
    'protocol': rdma,
    'devices': [
        { 'name': 'mlx5_2', 'lid': 17, 'gid': 'fe:00:...' },
        { 'name': 'mlx5_3', 'lid': 22, 'gid': 'fe:00:...' }
    ],
    'priority_matrix': {
        "cpu:0": [["mlx5_2"], ["mlx5_3"]],
        "cpu:1": [["mlx5_3"], ["mlx5_2"]],
        "cuda:0": [["mlx5_2"], ["mlx5_3"]],
    },
    'buffers': [
        {
            'name': 'cpu:0',
            'addr': 0x7fa16bdf5000,
            'length': 1073741824,
            'rkey': [1fe000, 1fdf00, ...], // 长度等同于 'devices' 字段的元素长度
        },
    ],
}

// 创建：命令行工具 register.py，确定可被挂载的文件路径。
// 修改：命令行工具 mount.py，向被挂载的 buffers.local_path_map 中添加挂载该文件的机器 -> 挂载机器上的文件路径的映射。
Key = mooncake/nvmeof/[segment_name]
Value = {
    'server_name': server_name,
    'protocol': nvmeof,
    'buffers':[ 
    {
        'length': 1073741824,
        'file_path': "/mnt/nvme0" // 本机器上的文件路径
        'local_path_map': {
            "node01": "/mnt/transfer_engine/node01/nvme0", // 挂载该文件的机器 -> 挂载机器上的文件路径
            ....
        },
     }，
     {
        'length': 1073741824,
        'file_path': "/mnt/nvme1", 
        'local_path_map': {
            "node02": "/mnt/transfer_engine/node02/nvme1",
            ....
        },
     }
    ]
}
```
</details>

## 构造函数与初始化

```cpp
TransferEngine(std::unique_ptr<TransferMetadata> metadata_client);
TransferMetadata(const std::string &metadata_server);
```

- TransferMetadata 对象指针，该对象将 TransferEngine 框架与元数据服务器/etcd 等带外通信逻辑抽取出来，以方便用户将其部署到不同的环境中。metadata_server 表示 etcd 服务器的 IP 地址或主机名。

为了便于异常处理，TransferEngine 在完成构造后需要调用init函数进行二次构造：
```cpp
int init(std::string& server_name, std::string& connectable_name, uint64_t rpc_port = 12345);
```
- server_name: 本地的 server name，保证在集群内唯一。它同时作为其他节点引用当前实例所属 RAM Segment 的名称（即 Segment Name）
- connectable_name：用于被其它 client 连接的 name，可为 hostname 或 ip 地址。
- rpc_port：用于与其它 client 交互的 rpc 端口。- 
- 返回值：若成功，返回 0；若 TransferEngine 已被 init 过，返回 -1。

```cpp
  ~TransferEngine();
```

回收分配的所有类型资源，同时也会删除掉全局 meta data server 上的信息。

## 范例程序
`mooncake-transfer-engine/example/transfer_engine_bench.cpp` 提供了一个样例程序，通过调用 Transfer Engine 接口，发起节点从目标节点的 DRAM 处反复读取/写入数据块，以展示 Transfer Engine 的基本用法，并可用于测量读写吞吐率。目前 Transfer Engine Bench 工具可用于 RDMA 及 TCP 协议。

## 二次开发
### 使用 C/C++ 接口二次开发
在完成 Mooncake Store 编译后，可将编译好的静态库文件 `libtransfer_engine.a` 及 C 头文件 `transfer_engine_c.h`，移入到你自己的项目里。不需要引用 `src/transfer_engine` 下的其他文件。

在项目构建阶段，需要为你的应用配置如下选项：
```
-I/path/to/include
-L/path/to/lib -ltransfer_engine
-lnuma -lglog -libverbs -ljsoncpp -letcd-cpp-api -lprotobuf -lgrpc++ -lgrpc
```

### 使用 Golang 接口二次开发
为了支撑 P2P Store 的运行需求，Transfer Engine 提供了 Golang 接口的封装，详见 `mooncake-p2p-store/src/p2pstore/transfer_engine.go`。

编译项目时启用 `-DWITH_P2P_STORE=ON` 选项，则可以一并编译 P2P Store 样例程序。

### 使用 Rust接口二次开发
在 `mooncake-transfer-engine/example/rust-example` 下给出了 TransferEngine 的 Rust 接口实现，并根据该接口实现了 Rust 版本的 benchmark，逻辑类似于 [transfer_engine_bench.cpp](../mooncake-transfer-engine/example/transfer_engine_bench.cpp)。若想编译 rust-example，需安装 Rust SDK，并在 cmake 命令中添加 `-DWITH_RUST_EXAMPLE=ON`。

## 高级运行时选项
对于高级用户，TransferEngine 提供了如下所示的高级运行时选项，均可通过 **环境变量（environment variable）** 方式传入。

- `MC_NUM_CQ_PER_CTX` 每个设备实例创建的 CQ 数量，默认值 1
- `MC_NUM_COMP_CHANNELS_PER_CTX` 每个设备实例创建的 Completion Channel 数量，默认值 1
- `MC_IB_PORT` 每个设备实例使用的 IB 端口号，默认值 1
- `MC_GID_INDEX` 每个设备实例使用的 GID 序号，默认值 3（或平台支持的最大值）
- `MC_MAX_CQE_PER_CTX` 每个设备实例中 CQ 缓冲区大小，默认值 4096
- `MC_MAX_EP_PER_CTX` 每个设备实例中活跃 EndPoint 数量上限，默认值 256
- `MC_NUM_QP_PER_EP` 每个 EndPoint 中 QP 数量，数量越多则细粒度 I/O 性能越好，默认值 2
- `MC_MAX_SGE` 每个 QP 最大可支持的 SGE 数量，默认值 4（或平台支持的最高值）
- `MC_MAX_WR` 每个 QP 最大可支持的 Work Request 数量，默认值 256（或平台支持的最高值）
- `MC_MAX_INLINE` 每个 QP 最大可支持的 Inline 写数据量（字节），默认值 64（或平台支持的最高值）
- `MC_MTU` 每个设备实例使用的 MTU 长度，可为 512、1024、2048、4096，默认值 4096（或平台支持的最大长度）
- `MC_WORKERS_PER_CTX` 每个设备实例对应的异步工作线程数量
- `MC_SLICE_SIZE` Transfer Engine 中用户请求的切分粒度
- `MC_RETRY_CNT` Transfer Engine 中最大重试次数
- `MC_VERBOSE` 若设置此选项，则在运行时会输出更详细的日志

