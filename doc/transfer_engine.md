# 设计目标

Mooncake Transfer Engine 是一个围绕 Segment 和 BatchTransfer 两个核心抽象设计的高性能，零拷贝数据传输库。其中 Segment 代表一段可被远程读写的连续地址空间，实际后端可以是 DRAM 或 VRAM 提供的非持久化存储 RAM Segment，也可以是 NVMeof 提供的持久化存储 NVMeof Segment。BatchTransfer 则负责将一个 Segment 中非连续的一组数据空间的数据和另外一组 Segment 的对应空间进行数据同步，支持 Read/Write 两种方向，因此类似一个异步且更灵活的的 AllScatter/AllGather。

![transfer_engine](fig/transfer_engine.png)

具体来说如上图所示一个特定的客户端对应一个 TransferEngine，其中不仅包含一个 RAM Segment 还集成了对于多线程多网卡高速传输的管理。RAM Segment 原则上就对应这个 TransferEngine 的全部虚拟地址空间，但实际上仅仅会注册其中的部分区域（被称为一个 Buffer）供外部 (GPUDirect) RDMA Read/Write。每一段 Buffer 可以分别设置权限（对应 RDMA rkey 等）和网卡亲和性（比如基于拓扑优先从哪张卡读写等）。

# 核心用户接口

TransferEngine 对外提供 TransferEngine 类，具体定义在 src/transfer_engine/transfer_engine.h 中。

## 数据传输

### TransferRequest

TransferEngine 提供的最核心 API 是：提交一组异步的 BatchTransfer 任务（submit_transfer）并查询其状态（get_transfer_status）。单个连续块的传输请求定义为一个 TransferRequest 及将本地总 source 开始长度为 length 的连续空间 Read/Write 到 target_id 对应 segment 从 target_offset 开始的空间。

结构体定义如下：

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

- Opcode 取值为 READ 或 WRITE。READ 表示数据从 <target_id, target_offset> 表示的地址复制到 source；WRITE 表示数据从 source 复制到 <target_id, target_offset> 表示的地址。
- Source 表示当前 TransferEngine 管理的 DRAM/VRAM buffer，必须提前已经被 registerLocalMemory() 接口注册
- Target_ID 表示传输目标的 Segment ID。Segment ID 的获取需要用到 getSegmentID() 接口。Segment 分为以下两种类型：
  - RAM 空间型，涵盖 DRAM/VRAM 两种形态。如前所述，同一进程（或者说是 TransferEngine 实例）下只有一个 Segment，这个 Segment 内含多种不同种类的 Buffer（DRAM/VRAM）。此时 getSegmentID() 接口传入的 Segment 名称等同于服务器主机名。target_offset 为目标服务器的虚拟地址。
  - NVMeOF 空间型，每个文件对应一个 Segment。此时 getSegmentID() 接口传入的 Segment 名称等同于文件的唯一标识符。target_offset 为目标文件的偏移量。
- length 表示传输的数据量。TransferEngine 在内部可能会进一步拆分成多个读写请求。

### TransferEngine::allocateBatchID

```cpp
BatchID TransferEngine::allocateBatchID(size_t batch_size);
```

分配 BatchID。同一 BatchID 下最多可提交 batch_size 个 TransferRequest。

- batch_size: 同一 BatchID 下最多可提交的 TransferRequest 数量；
- 返回值：若成功，返回 BatchID（非负）；否则返回负数值。

### TransferEngine::submitTransfer

```cpp
int TransferEngine::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries);
```

向 batch_id 追加提交新的 Transfer 任务。为异步操作，提交到后台线程池后立即返回。同一 batch_id 下累计的 entries 数量不应超过创建时定义的 batch_size。

- batch_id: 所属的 BatchID ；
- entries: Transfer 任务数组；
- 返回值：若成功，返回 0；否则返回负数值。

### TransferEngine::getTransferStatus

```cpp
enum TaskStatus
{
  WAITING, // 正在处于传输阶段
  PENDING,
  INVALID, // 参数不合法
  CANNELED, // 暂不支持
  COMPLETED, // 传输完毕
  TIMEOUT, // 暂不支持
  FAILED // 即使经过重试仍传输失败
};
struct TransferStatus {
  TaskStatus s;
  size_t transferred; // 已成功传输了多少数据（不一定是准确值，确保是 lower bound）
};
int TransferEngine::getTransferStatus(BatchID batch_id, std::vector<TransferStatus> &status)
```

获取 batch_id 对应所有 TransferRequest 的运行状态。

- batch_id: 所属的 BatchID ；
- status: 输出 Transfer 状态数组，其长度等于该 BatchID 中通过 submitTransfer 累计传入的 TransferRequest 数量。目前；
- 返回值：若成功，返回 0；否则返回负数值。

### TransferEngine::freeBatchID

```cpp
int freeBatchID(BatchID batch_id);
```

回收 BatchID，之后对此的 submitTransfer 及 getTransferStatus 操作均是未定义的。若 BatchID 内仍有 TransferRequest 未完成（即处于 WAITING），则拒绝操作。

- batch_id: 所属的 BatchID ；
- 返回值：若成功，返回 0；否则返回负数值。

## 空间注册

上述传输过程作为源端指针的 TransferRequest::source，理论上可以是当前进程虚拟空间中的任何一个指向 DRAM 或 VRAM 的指针，但必须提前注册为 RDMA 可读写的 Memory Region 空间。因此提供如下的辅助函数：

### TransferEngine::registerLocalMemory

```cpp
int TransferEngine::registerLocalMemory(void \*addr, size_t size, string location);
```

在本地 DRAM/VRAM 上注册起始地址为 addr，长度为 size 的空间。

- addr: 注册空间起始地址；
- size：注册空间长度；
- location: 这一段内存对应的 device 比如 cuda:0 表示对应 GPU，cpu:0 表示对应 socket，通过和 PriorityMatrix（见构造函数一节） 匹配可用的 RNIC 列表，用于识别优选的网卡。
- 返回值：若成功，返回 0；否则返回负数值。

### TransferEngine::unregisterLocalMemory

```cpp
int TransferEngine::unregisterLocalMemory(void *addr);
```

解注册区域。

- addr: 注册空间起始地址；
- 返回值：若成功，返回 0；否则返回负数值。

## Segment 管理

前文指出构造 TransferRequest 的过程中仅仅需要传递 segment_id 就可以代表一个远端的 Segment。在 RDMA 连接期间，为了完成对应连接的建立需要维护并交换 RDMA 和多网卡亲和性相关的元数据。TransferEngine 内部使用 TransferMetadata 模块实现编解码功能，结合中心化全局的 metadata server（当前由 memcached/etcd 实现）及节点之间互相传递实现上述目标。

在构造 TransferEngine 对象期间，会向 memcached/etcd 写入一条新的 SegmentDesc 记录，并按需要进行更新。

适用于 RDMA 模式的 SegmentDesc 涵盖了所有 RDMA 网卡的信息、优先矩阵及 Buffer 列表。

```cpp
struct DeviceDesc
{
    std::string name; // 网卡名称（如 mlx5_3）
    uint16_t lid;
    std::string gid;
};

struct BufferDesc
{
    std::string name; // Buffer 类别名称，如“cpu:0”等
    uint64_t addr; // 起始虚拟地址
    uint64_t length; // 长度
    std::vector<uint32_t> lkey;
    std::vector<uint32_t> rkey;
};

struct PriorityItem
{
    std::vector<std::string> preferred_rnic_list; // 对于指定类别的 Buffer，优先使用的网卡名称
    std::vector<std::string> available_rnic_list; // 对于指定类别的 Buffer，次使用的网卡名称
};

using PriorityMatrix = std::unordered_map<std::string, PriorityItem>;

struct SegmentDesc
{
    std::string name; // Segment 的名称，对于 RDMA Segment 同所述服务器名称
    std::vector<DeviceDesc> devices; // priority_matrix 所用的所有网卡设备信息
    PriorityMatrix priority_matrix; // 优先矩阵
    std::vector<BufferDesc> buffers; // BufferDesc 表示一段连续的注册内存空间
};
```

存储在 memcached/etcd 的 JSON 数据如下

```cpp
Key = mooncake/[server_name]
Value= {
    'name': 'optane20'
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

## 构造函数

```cpp
TransferEngine(std::unique_ptr<TransferEngineMetadataClient> metadata_client,
const std::string &local_server_name,
const std::string &nic_priority_matrix);
```

- metadata_client：TransferEngineMetadataClient 对象指针，该对象将 TransferEngine 框架与元数据服务器/etcd 等带外通信逻辑抽取出来，以方便用户将其部署到不同的环境中。
- local_server_name：标识本地 CLIENT 的名称。集群内 local_server_name 的值应当具备唯一性。推荐使用 hostname。
- nic_priority_matrix：是一个 JSON 字符串，表示使用的存储介质名称及优先使用的网卡列表，样例如下

```cpp
{
    "cpu:0": [["mlx0", "mlx1"], ["mlx2", "mlx3"]],
    "cuda:0": [["mlx1", "mlx0"]],
    ...
}
```

- 其中每个 key 代表一个 CPU socket 或者一个 GPU device 对应的设备名称
- 每个 value 为一个 (prefered_nic, accessable_nic) 的二元组，每一项都是一个 nic 名称的 list。 - prefer_nic 表示优先选择的 NIC，比如对于 CPU 可以是当前直连而非跨 NUMA 的 NIC，对于 GPU 可以是挂在同一个 PCIe Switch 下的 NIC - accessable_nic 表示虽然不优选但是理论上可以连接上的 NIC，用于故障重试时使用

```cpp
  ~TransferEngine();
```

回收分配的所有类型资源，同时也会删除掉全局 meta data server 上的信息。

## RPC

### ExchangeQPN

用于交换 QPNum 来建立 RDMA 连接。

```cpp
    struct HandShakeDesc
    {
        std::string local_nic_path; // 请求方的 server_name@nic_name
        std::string peer_nic_path; // 接收方的 server_name@nic_name
        std::vector<uint32_t> qp_num;
    };
    /*
    JSON 格式
    value = {
        'local_nic_path': 'optane20@mlx5_2',
        'peer_nic_path': 'optane21@mlx5_2',
        'qp_num': [xxx, yyy]
    }
    */

    int TransferEngine::onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc);
```

为实现 RDMA 通联，需要将新 Segment 所属 CLIENT 与集群内原有 CLIENT 之间建立 QP 配对，以建立点对点可靠连接。例如，请求方 server1@mlx5_1 想和远端 server2@mlx5_2 建立连接，则 server1 会主动向 server2 发出 HandShakeDesc 消息，server2 需要接收消息并执行 onSetupRdmaConnections() 更新自身的 QP 状态，同时以自身状态为基础传回 HandShakeDesc 消息，最后 server1 同样更新自身的 QP 状态，完成连接操作。

- request_qp_reg_desc：传入的请求方（远程）RDMA QP 注册标识信息（LID、GID、QPN）
- response_qp_reg_desc：传出的响应方（本地）RDMA QP 注册标识信息（LID、GID、QPN）
- 返回值：若成功，返回 0；否则返回负数值。

### SubmitTransfer

TBD —— 组合了 allocateBatchID 和 submitTransfer 语义。

### GetStatus

TBD —— 组合了 getTransferStatus 和 freeBatchID 语义（若第一次返回 SUCCESS 或 FAILED，则可以立即释放空间）。

## EndPoint

RdmaEndPoint 表示本地 NIC1 (由所属的 RdmaContext 确定) 与远端 NIC2 (由 peer_nic_path 确定) 之间的所有 QP 连接。构造完毕后（RdmaEndPoint::RdmaEndPoint() 和 RdmaEndPoint::construct()），相应资源被分配，但不指定对端，随后需要与对手方交换 Handshake 信息，RdmaEndPoint 才能正确发送工作请求。

- 主动发起握手的一方，调用 setupConnectionsByActive 函数，传入对端的 peer_nic_path。peer_nic_path 的格式如下：peer_nic_path := peer_server_name@nic_name。如 optane20@mlx5_3，可由对端 RdmaContext::nicPath() 取得
- 被动触发握手的一方，由 TransferMetadata 监听线程调用 setupConnectionsByPassive 函数。对端的 peer_nic_path 位于 peer_desc.local_nic_path 内

  完成上述操作后，RdmaEndPoint 状态被设置为 CONNECTED
  用户主动调用 disconnect() 或内部检测到错误时，连接作废，RdmaEndPoint 状态被设置为 UNCONNECTED。此时可以重新触发握手流程
