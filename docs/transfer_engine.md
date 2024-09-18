# Mooncake Transfer Engine 

Mooncake Transfer Engine 是一个围绕 Segment 和 BatchTransfer 两个核心抽象设计的高性能，零拷贝数据传输库。其中 Segment 代表一段可被远程读写的连续地址空间，实际后端可以是 DRAM 或 VRAM 提供的非持久化存储 RAM Segment，也可以是 NVMeof 提供的持久化存储 NVMeof Segment。BatchTransfer 则负责将一个 Segment 中非连续的一组数据空间的数据和另外一组 Segment 的对应空间进行数据同步，支持 Read/Write 两种方向，因此类似一个异步且更灵活的的 AllScatter/AllGather。

![transfer_engine](fig/transfer_engine.png)

具体来说，如上图所示，每个特定的客户端对应一个 TransferEngine，其中不仅包含一个 RAM Segment，还集成了对于多线程多网卡高速传输的管理。RAM Segment 原则上就对应这个 TransferEngine 的全部虚拟地址空间，但实际上仅仅会注册其中的部分区域（被称为一个 Buffer）供外部 (GPUDirect) RDMA Read/Write。每一段 Buffer 可以分别设置权限（对应 RDMA rkey 等）和网卡亲和性（比如基于拓扑优先从哪张卡读写等）。

# 核心用户接口

Mooncake Transfer Engine 通过 `TransferEngine` 类对外提供接口（位于 `mooncake-transfer-engine/include/transfer_engine.h`），其中对应不同后端的具体的数据传输功能由 `Transport` 类实现，目前支持 `RdmaTransport` 和 `NVMeoFTransport`。

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
BatchID Transport::allocateBatchID(size_t batch_size);
```

分配 `BatchID`。同一 `BatchID` 下最多可提交 `batch_size` 个 `TransferRequest`。

- `batch_size`: 同一 `BatchID` 下最多可提交的 `TransferRequest` 数量；
- 返回值：若成功，返回 `BatchID`（非负）；否则返回负数值。

### Transport::submitTransfer

```cpp
int Transport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries);
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
int Transport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
```

获取 `batch_id` 中第 `task_id` 个 `TransferRequest` 的运行状态。

- `batch_id`: 所属的 `BatchID`；
- `task_id`: 要查询的 `TransferRequest` 序号；
- `status`: 输出 Transfer 状态；
- 返回值：若成功，返回 0；否则返回负数值。

### Transport::freeBatchID

```cpp
int Transport::freeBatchID(BatchID batch_id);
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

对于 RDMA 的传输过程，作为源端指针的 `TransferRequest::source` 必须提前注册为 RDMA 可读写的 Memory Region 空间。因此提供如下的辅助函数：

### TransferEngine::registerLocalMemory

```cpp
int TransferEngine::registerLocalMemory(void *addr, size_t size, string location, bool remote_accessible);
```

在本地 DRAM/VRAM 上注册起始地址为 `addr`，长度为 `size` 的空间。

- `addr`: 注册空间起始地址；
- `size`：注册空间长度；
- `location`: 这一段内存对应的 `device`，比如 `cuda:0` 表示对应 GPU 设备，`cpu:0` 表示对应 CPU socket，通过和网卡优先级顺序表（见`installOrGetTransport`） 匹配，识别优选的网卡。
- `remote_accessible`: 标识这一块内存能否被远端节点访问。
- 返回值：若成功，返回 0；否则返回负数值。

### TransferEngine::unregisterLocalMemory

```cpp
int TransferEngine::unregisterLocalMemory(void *addr);
```

解注册区域。

- addr: 注册空间起始地址；
- 返回值：若成功，返回 0；否则返回负数值。

## Segment 管理与 ETCD 元数据

### Segment 管理
TranferEngine 提供 `openSegment` 函数，该函数获取一个 `SegmentHandle`，用于后续 `Transport` 的传输。
```cpp
SegmentHandle openSegment(const std::string& segment_name);
```
- `segment_name`：segment 的唯一标志符。
- 返回值：若成功，返回对应的 SegmentHandle；否则返回负数值。
  
```cpp
int closeSegment(SegmentHandle segment_id);
```
- `segment_id`：segment 的唯一标志符。
- 返回值：若成功，返回 0；否则返回负数值。

### ETCD 元数据形态
```
// 用于根据 server_name 查找可通信的地址以及暴露的 rpc 端口。
// 创建：调用 TransferEngine::init() 时。
// 删除：TransferEngine 被析构时。
Key = mooncake/rpc_meta/[server_name]
Value = {
    'ip_or_host_name': 'optane20'
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
            "optane13": "/mnt/transfer_engine/optane14/nvme0", // 挂载该文件的机器 -> 挂载机器上的文件路径
            ....
        },
     }，
     {
        'length': 1073741824,
        'file_path': "/mnt/nvme1", 
        'local_path_map': {
            "optane13": "/mnt/transfer_engine/optane14/nvme1",
            ....
        },
     }
    ]
}

// 更多不需要依赖运行时信息即可完整注册的存储介质。
Key = mooncake/s3/[segment_name]
Value = {
    ...
}
```

TransferMetadata 的下列接口用于上传和下载 SegmentDesc 项目。

```cpp
int updateSegmentDesc(const std::string &server_name, const SegmentDesc &desc);
std::shared_ptr<SegmentDesc> getSegmentDesc(const std::string &server_name);
int removeSegmentDesc(const std::string &server_name);
```

TransferEngine 的下列操作会影响位于 memcached/etcd 的 SegmentDesc 记录

- 构造函数：创建 key=mooncake/server_name 的新项。分析并上传用户提供的 priority_matrix，并分配所需的 RDMA 设备对象
- registerLocalMemory 函数则会向当前对象对应的 SegmentDesc 记录中加入一个新的 BufferDesc
- unregisterLocalMemory 函数则会向当前对象对应的 SegmentDesc 记录中删除对应的 BufferDesc
- 析构函数：删除 key=mooncake/server_name 的新项

## 构造函数与初始化

```cpp
TransferEngine(std::unique_ptr<TransferEngineMetadataClient> metadata_client);
```

- metadata_client：TransferEngineMetadataClient 对象指针，该对象将 TransferEngine 框架与元数据服务器/etcd 等带外通信逻辑抽取出来，以方便用户将其部署到不同的环境中。

为了便于异常处理，TransferEngine 在完成构造后需要调用init函数进行二次构造：
```cpp
int init(std::string& server_name, std::string& connectable_name, uint64_t rpc_port = 12345);
```
- server_name: 本地的 server name，保证在集群内唯一。
- connectable_name：用于被其它 client 连接的 name，可为 hostname 或 ip 地址。
- rpc_port：用于与其它 client 交互的 rpc 端口。- 
- 返回值：若成功，返回 0；若 TransferEngine 已被 init 过，返回 -1。

```cpp
  ~TransferEngine();
```

回收分配的所有类型资源，同时也会删除掉全局 meta data server 上的信息。

## 封装成的 C 接口
```cpp
// transfer_engine_c.h
extern "C" {
    // 将 TransferEngine 和 Transport 类的接口改写成 C，以便上层由其它语言编写的服务调用。
    // 除了需要将 engine / transport 作为参数传入，其它均与对应的 C++ 接口等价。

    typedef struct transfer_status transfer_status_t;
    typedef void * transfer_engine_t;
    typedef void * transport_t;

    transfer_engine_t createTransferEngine(const char *metadata_uri);
    int initTransferEngine(transfer_engine_t engine，
                           const char* server_name);

    transport_t installOrGetTransport(transfer_engine_t engine, 
                                      const char *proto,
                                      void **args);

    segment_handle_t openSegment(transfer_engine_t engine,
                                 const char *segment_name, 
                                 transport_t *transport);

    int closeSegment(transfer_engine_t engine, 
                     segment_id_t segment_id);

    void destroyTransferEngine(transfer_engine_t engine);

    int registerLocalMemory(transfer_engine_t engine,
                            void *addr,
                            size_t size,
                            const char* location,
                            bool remote_accessible = false);

    batch_id_t allocateBatchID(transport_t xport,
                               size_t batch_size);

    int submitTransfer(transport_t xport,
                       batch_id_t batch_id,
                       struct transfer_request *entries,
                       size_t count);

    int getTransferStatus(transport_t xport,
                          batch_id_t batch_id,
                          size_t task_id,
                          struct transfer_status *status);

    int freeBatchID(transport_t xport, batch_id_t batch_id);
}
```
## 利用 TransferEngine 进行二次开发
要利用 TransferEngine 进行二次开发，可使用编译好的静态库文件 `libtransfer_engine.a` 及 C 头文件 `transfer_engine_c.h`，不需要用到 `src/transfer_engine` 下的其他文件。


## 样例程序使用
样例程序的代码见 `tests/transfer_engine_test.cpp`。使用要点如下:

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

