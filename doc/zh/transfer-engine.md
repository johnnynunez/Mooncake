# Transfer Engine

Mooncake Transfer Engine 是一个围绕 Segment 和 BatchTransfer 两个核心抽象设计的高性能，零拷贝数据传输库。

- **Segment** 代表一段可被远程读写的连续地址空间，既可以是 DRAM 或 VRAM 提供的非持久化存储 **RAM Segment**，也可以是 NVMeof 提供的持久化存储 **NVMeof Segment**。

- **BatchTransfer** 封装了操作请求，具体负责将一个 Segment 中非连续的一组数据空间的数据和另外一组 Segment 的对应空间进行数据同步，支持 Read/Write 两种方向，因此类似一个异步且更灵活的的 AllScatter/AllGather。

![transfer_engine](../../image/transfer-engine.png)

如上图所示，每个特定的客户端对应一个 TransferEngine，其中不仅包含一个 RAM Segment，还集成了对于多线程多网卡高速传输的管理。RAM Segment 原则上就对应这个 TransferEngine 的全部虚拟地址空间，但实际上仅仅会注册其中的部分区域（被称为一个 Buffer）供外部 (GPUDirect) RDMA Read/Write。每一段 Buffer 可以分别设置权限（对应 RDMA rkey 等）和网卡亲和性（比如基于拓扑优先从哪张卡读写等）。

Mooncake Transfer Engine 通过 `TransferEngine` 类对外提供接口（位于 `mooncake-transfer-engine/include/transfer_engine.h`），其中对应不同后端的具体的数据传输功能由 `Transport` 类实现，目前支持 `TcpTransport`、`RdmaTransport` 和 `NVMeoFTransport`。

## Segment
Segment 表示 Transfer Engine 实现数据传输过程期间可使用的源地址范围及目标地址范围集合。也就是说，所有 BatchTransfer 请求中涉及的本地与远程地址都需要位于合法的 Segment 区间里。Transfer Engine 支持以下两种类型的 Segment。

### 1. 位于内存地址空间（DRAM、VRAM）的 RAM Segment
每一个进程启动时， Transfer Engine 会自动创建一个以自身 `local_hostname` 为名称（见 TransferEngine 的初始化函数，需要全局唯一）的 Segment，该 Segment 在逻辑上覆盖了完整的内存地址空间，包括 DRAM/VRAM 等存储介质，Transfer Engine 在使用 BatchTransfer 接口进行传输任务时，会自动判断相应硬件信息，从而选择最佳传输方式。每个进程有且只有一个 Segment。其他进程通过调用 `openSegment` 接口并传递正确名称的方式，可引用 Segment 并完成读写操作。

在实际部署中，应用系统通常只使用部分内存地址空间完成数据传输，因此在 Transfer Engine 内部将 Segment 进一步划分成多个 Buffers。每个 Buffer 代表一段连续的、位于同一设备上的地址空间，用户使用 BatchTransfer 接口完成读写操作时，若引用 RAM Segment，则每次读写任务的范围必须在其中的某一个合法 Buffer 内。
一个 Segment 内的内存范围不需要是连续的，也就是说，可以通过分配多段DRAM/VRAM 地址空间并纳入相同的 Segment。

除此在外，Transfer Engine 也支持注册一些本地 DRAM 区域，这一部分区域仅仅是作为数据操作的本地侧存储空间，比如 vLLM 的 DRAM PageCache 区域。它也被视为当前进程中有效 RAM Segment 的一部分，但不能被其他进程通过调用 `openSegment` 接口引用。

### 2. 位于挂载到 NVMeof 上文件的 **NVMeof Segment**
Transfer Engine 也借助 NVMeof 协议，支持从 NVMe 上直接将文件指定偏移的数据，通过 PCIe 直传方式直达 DRAM/VRAM，无需经过 CPU 且实现零拷贝。用户需要按照指引的说明将远程存储节点挂载到本地，并使用 `openSegment` 接口进行引用，从而完成数据读写操作。

## BatchTransfer

借助 Transfer Engine，Mooncake Store 可实现本地 DRAM/VRAM 通过(GPUDirect) RDMA、NVMe-of 协议等读写本地/远端的有效 Segment（即注册的 DRAM/VRAM 区间及 NVMe 文件）中的指定部分。

| 远程 ↓ 本地→ | DRAM | VRAM |
|----------|------|------|
| DRAM     | ✓    | ✓    |
| VRAM     | ✓    | ✓    |
| NVMe-of  | ✓    | ✓    |

- Local memcpy: 如果目标 Segment 其实就在本地 DRAM/VRAM 的话，直接使用 memcpy、cudaMemcpy 等数据复制接口。
- RDMA: 支持本地 DRAM/VRAM 与远程 DRAM 之间的数据传递。在实现上支持多网卡池化及重试等功能。
- cuFile (GPUDirect Storage): 实现本地 DRAM/VRAM 与 Local/Remote NVMeof 之间的数据传递。

BatchTransfer API 使用请求（Request）对象数组传入用户请求，需指定操作类型（READ 或 WRITE）、数据长度以及本地和远程内存的地址。传输操作适用于 DRAM 和 GPU VRAM，并在最佳情况下利用 GPU 直接 RDMA，前提是指定的内存区域已预先注册。这些操作的完成情况可通过 `getTransferStatus` API 来异步监控这些操作的完成情况。

## 拓扑感知路径选择（Topology Aware Path Selection）
现代推理服务器通常由多个CPU插槽、DRAM、GPU和RDMA NIC设备组成。尽管从技术上讲，使用任何RDMA NIC将数据从本地DRAM或VRAM传输到远程位置是可能的，但这些传输可能会受到Ultra Path Interconnect (UPI)或PCIe交换机带宽限制的制约。为了克服这些限制，Transfer Engine 实现了拓扑感知路径选择算法。在处理请求之前，每个服务器生成一个拓扑矩阵（Topology Matrix）并将其广播到整个集群。拓扑矩阵将网络接口卡（NIC）分类为各种类型的内存的“首选”和“次要”列表，这些类型在内存注册时指定。在正常情况下，选择首选列表中的NIC进行传输，便于在本地NUMA或仅通过本地PCIe交换机进行GPU Direct RDMA操作。在出现故障的情况下，两个列表中的所有NIC都可能被使用。上述过程包括根据内存地址识别适当的本地和目标NIC，建立连接，并执行数据传输。

![topology-matrix](../../image/topology-matrix.png)

例如，如图所示，要将数据从本地节点分配给 `cpu:0` 的缓冲区0传输到目标节点分配给`cpu:1`的缓冲区1，引擎首先使用本地服务器的拓扑矩阵识别`cpu:0`的首选NIC，并选择一个，如`mlx5_1`，作为本地NIC。同样，根据目标内存地址选择目标NIC，如`mlx5_3`。这种设置允许建立从`mlx5_1@本地`到`mlx5_3@目标`的RDMA连接，以执行RDMA读写操作。

为了进一步最大化带宽利用率，如果单个请求的传输长度超过16KB，则其内部被划分为多个切片。每个切片可能使用不同的路径，使所有RDMA NIC能够协同工作。

## 端点管理
Transfer Engine 使用一对端点来表示本地RDMA NIC和远程RDMA NIC之间的连接。实际上，每个端点包括一个或多个RDMA QP对象。
Transfer Engine 中的连接是按需建立的；端点在第一次请求之前保持未配对状态。
为了防止大量端点减慢请求处理速度，Transfer Engine 采用端点池，限制最大活动连接数。
Transfer Engine 使用SIEVE算法来管理端点的逐出。如果由于链路错误导致连接失败，它将从两端的端点池中移除，并在下一次数据传输尝试期间重新建立。

## 故障处理
在多NIC环境中，一个常见的故障场景是特定NIC的暂时不可用，而其他路由仍然可以连接两个节点。Transfer Engine 旨在有效地管理这种暂时性故障。如果识别到连接不可用，Transfer Engine 会自动识别一个替代的、可达的路径，并将请求重新提交给不同的RDMA NIC设备。此外，Transfer Engine 能够检测到其他RDMA资源的问题，包括RDMA上下文和完成队列。它会暂时避免使用这些资源，直到问题得到解决。
