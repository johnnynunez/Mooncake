# Transfer Engine

Mooncake Transfer Engine is a high-performance, zero-copy data transfer library designed around two core abstractions: Segment and BatchTransfer.

- **Segment** represents a contiguous address space that can be remotely read and written, which can be either non-persistent storage provided by DRAM or VRAM, known as **RAM Segment**, or persistent storage provided by NVMeof, known as **NVMeof Segment**.

- **BatchTransfer** encapsulates operation requests, specifically responsible for synchronizing data between a set of non-contiguous data spaces in one Segment and the corresponding spaces in another set of Segments, supporting Read/Write in both directions, thus acting like an asynchronous and more flexible AllScatter/AllGather.

![transfer_engine](../../image/transfer-engine.png)

As shown in the diagram, each specific client corresponds to a TransferEngine, which not only includes a RAM Segment but also integrates management for high-speed transfers across multiple threads and network cards. The RAM Segment, in principle, corresponds to the entire virtual address space of this TransferEngine, but in reality, only parts of it (known as a Buffer) are registered for external (GPUDirect) RDMA Read/Write. Each Buffer can have separate permissions (corresponding to RDMA rkey, etc.) and network card affinity (e.g., based on topology,优先从哪张卡读写等).

Mooncake Transfer Engine provides interfaces through the `TransferEngine` class (located in `mooncake-transfer-engine/include/transfer_engine.h`), where the specific data transfer functions for different backends are implemented by the `Transport` class, currently supporting `TcpTransport`, `RdmaTransport`, and `NVMeoFTransport`.

## Segment
Segment represents a collection of source address ranges and target address ranges available during the data transfer process in Transfer Engine. That is, all local and remote addresses involved in BatchTransfer requests must be within the legal Segment range. Transfer Engine supports the following two types of Segments.

### 1. RAM Segment in Memory Address Space (DRAM, VRAM)
When each process starts, Transfer Engine automatically creates a Segment named after its own `local_hostname` (see the initialization function of TransferEngine, which needs to be globally unique), which logically covers the entire memory address space, including storage media such as DRAM/VRAM. When using the BatchTransfer interface for transfer tasks, Transfer Engine automatically determines the corresponding hardware information to choose the best transfer method. Each process has and only has one Segment. Other processes can reference the Segment and complete read/write operations by calling the `openSegment` interface and passing the correct name.

In actual deployment, application systems usually only use part of the memory address space for data transfer, so Transfer Engine further divides the Segment into multiple Buffers internally. Each Buffer represents a contiguous address space located on the same device, and when users use the BatchTransfer interface to complete read/write operations, if referencing a RAM Segment, each read/write task must be within one of the legal Buffers.

The memory range within a Segment does not need to be contiguous, which means that multiple DRAM/VRAM address spaces can be allocated and included in the same Segment.

In addition, Transfer Engine also supports registering some local DRAM areas, which are merely used as the local side storage space for data operations, such as the DRAM PageCache area of vLLM. It is also considered a part of the effective RAM Segment in the current process but cannot be referenced by other processes by calling the `openSegment` interface.

### 2. NVMeof Segment Located on Files Mounted to NVMeof
Transfer Engine also leverages the NVMeof protocol to support direct data transfer from files on NVMe to DRAM/VRAM via PCIe, without going through the CPU and achieving zero-copy. Users need to follow the instructions to mount remote storage nodes locally and use the `openSegment` interface for reference to complete data read/write operations.

## BatchTransfer

With the help of Transfer Engine, Mooncake Store can achieve local DRAM/VRAM reading and writing of specified parts in effective Segments (i.e., registered DRAM/VRAM intervals and NVMe files) through (GPUDirect) RDMA, NVMe-of protocols, etc.

| Remote ↓ Local → | DRAM | VRAM |
|----------|------|------|
| DRAM     | ✓    | ✓    |
| VRAM     | ✓    | ✓    |
| NVMe-of  | ✓    | ✓    |

- Local memcpy: If the target Segment is actually in the local DRAM/VRAM, direct data copy interfaces such as memcpy, cudaMemcpy are used.
- RDMA: Supports data transfer between local DRAM/VRAM and remote DRAM. It supports multi-network card pooling and retry functions in implementation.
- cuFile (GPUDirect Storage): Implements data transfer between local DRAM/VRAM and Local/Remote NVMeof.

The BatchTransfer API uses an array of request (Request) objects to pass user requests, specifying the operation type (READ or WRITE), data length, and local and remote memory addresses. The transfer operation is applicable to DRAM and GPU VRAM, and in the best case, utilizes GPU direct RDMA, provided that the specified memory area has been pre-registered. The completion of these operations can be asynchronously monitored through the `getTransferStatus` API.

## Topology Aware Path Selection
Modern inference servers are usually composed of multiple CPU sockets, DRAM, GPUs, and RDMA NIC devices. Although technically, it is possible to use any RDMA NIC to transfer data from local DRAM or VRAM to a remote location, these transfers may be constrained by the bandwidth limits of Ultra Path Interconnect (UPI) or PCIe switches. To overcome these limitations, Transfer Engine implements a topology-aware path selection algorithm. Before processing requests, each server generates a topology matrix and broadcasts it to the entire cluster. The topology matrix categorizes network interface cards (NICs) into "preferred" and "secondary" lists for various types of memory, specified during memory registration. Under normal circumstances, the preferred list NIC is chosen for transfer, facilitating GPU Direct RDMA operations within local NUMA or only through local PCIe switches. In the event of a failure, all NICs in both lists may be used. The process includes identifying the appropriate local and target NICs based on memory addresses, establishing connections, and executing data transfers.

![topology-matrix](../../image/topology-matrix.png)

For example, as shown in the diagram, to transfer data from the buffer 0 allocated to `cpu:0` on the local node to buffer 1 allocated to `cpu:1` on the target node, the engine first uses the topology matrix of the local server to identify the preferred NIC for `cpu:0`, and selects one, such as `mlx5_1`, as the local NIC. Similarly, the target NIC is selected based on the target memory address, such as `mlx5_3`. This setup allows the establishment of an RDMA connection from `mlx5_1@local` to `mlx5_3@target` to perform RDMA read/write operations.

To further maximize bandwidth utilization, if the transfer length of a single request exceeds 16KB, it is internally divided into multiple slices. Each slice may use a different path, allowing all RDMA NICs to work together.

## Endpoint Management
Transfer Engine uses a pair of endpoints to represent the connection between local RDMA NIC and remote RDMA NIC. In fact, each endpoint includes one or more RDMA QP objects.
Connections in Transfer Engine are established on demand; endpoints remain unpaired before the first request.
To prevent a large number of endpoints from slowing down request processing, Transfer Engine uses an endpoint pool to limit the maximum number of active connections.
Transfer Engine uses the SIEVE algorithm to manage the eviction of endpoints. If a connection fails due to a link error, it will be removed from the endpoint pool on both ends and re-established during the next data transfer attempt.

## Fault Handling
In a multi-NIC environment, a common failure scenario is the temporary unavailability of a specific NIC, while other routes can still connect the two nodes. Transfer Engine is designed to effectively manage this kind of temporary failure. If a connection is identified as unavailable, Transfer Engine will automatically identify an alternative, reachable path and resubmit the request to a different RDMA NIC device. In addition, Transfer Engine can detect other RDMA resource issues, including RDMA contexts and completion queues. It will temporarily avoid using these resources until the problem is resolved.
