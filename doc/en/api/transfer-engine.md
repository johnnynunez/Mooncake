# Transfer Engine C/C++ API

**[Transfer Engine](doc/zh/architecture.md#transfer-engine) is a high-performance, zero-copy data transfer library with two core abstractions: Segment and BatchTransfer.**

Transfer Engine provides interfaces through the `TransferEngine` class (located in `mooncake-transfer-engine/include/transfer_engine.h`), where the specific data transfer functions for different backends are implemented by the `Transport` class, currently supporting `TcpTransport`, `RdmaTransport` and `NVMeoFTransport`.

## Data Transfer

### Transport::TransferRequest

The core API provided by Mooncake Transfer Engine is submitting a group of asynchronous `Transport::TransferRequest` tasks through the `Transport::submitTransfer` interface, and querying their status through the `Transport::getTransferStatus` interface. Each `Transport::TransferRequest` specifies reading or writing a continuous data space of `length` starting from the local starting address `source`, to the position starting at `target_offset` in the segment corresponding to `target_id`.

The `Transport::TransferRequest` structure is defined as follows:

```cpp
using SegmentID = int32_t;
struct TransferRequest
{
    enum OpCode { READ, WRITE };
    OpCode opcode;
    void *source;
    SegmentID target_id; // The ID of the target segment, which may correspond to local or remote DRAM/VRAM/NVMeof, with the specific routing logic hidden
    size_t target_offset;
    size_t length;
};
```

- `opcode` takes the values `READ` or `WRITE`. `READ` indicates that data is copied from the target address indicated by `<target_id, target_offset>` to the local starting address `source`; `WRITE` indicates that data is copied from `source` to the address indicated by `<target_id, target_offset>`.
- `source` represents the DRAM/VRAM buffer managed by the current `TransferEngine`, which must have been registered in advance by the `registerLocalMemory` interface.
- `target_id` represents the Segment ID of the transfer target. The Segment ID is obtained using the `openSegment` interface. Segments are divided into the following types:
  - RAM space type, covering DRAM/VRAM. As mentioned earlier, there is only one Segment under the same process (or `TransferEngine` instance), which contains various types of Buffers (DRAM/VRAM). In this case, the Segment name passed to the `openSegment` interface is equivalent to the server hostname. `target_offset` is the virtual address of the target server.
  - NVMeOF space type, where each file corresponds to a Segment. In this case, the Segment name passed to the `openSegment` interface is equivalent to the unique identifier of the file. `target_offset` is the offset of the target file.
- `length` represents the amount of data transferred. TransferEngine may further split this into multiple read/write requests internally.

### Transport::allocateBatchID

```cpp
BatchID allocateBatchID(size_t batch_size);
```

Allocates a `BatchID`. A maximum of `batch_size` `TransferRequest`s can be submitted under the same `BatchID`.

- `batch_size`: The maximum number of `TransferRequest`s that can be submitted under the same `BatchID`;
- Return value: If successful, returns `BatchID` (non-negative); otherwise, returns a negative value.

### Transport::submitTransfer

```cpp
int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries);
```

Submits new `TransferRequest` tasks to `batch_id`. The task is asynchronously submitted to the background thread pool. The total number of `entries` accumulated under the same `batch_id` should not exceed the `batch_size` defined at creation.

- `batch_id`: The `BatchID` it belongs to;
- `entries`: Array of `TransferRequest`;
- Return value: If successful, returns 0; otherwise, returns a negative value.

### Transport::getTransferStatus

```cpp
enum TaskStatus
{
  WAITING,   // In the transfer phase
  PENDING,   // Not supported
  INVALID,   // Illegal parameters
  CANNELED,  // Not supported
  COMPLETED, // Transfer completed
  TIMEOUT,   // Not supported
  FAILED     // Transfer failed even after retries
};
struct TransferStatus {
  TaskStatus s;
  size_t transferred; // How much data has been successfully transferred (not necessarily an accurate value, but it is a lower bound)
};
int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
```

Obtains the running status of the `TransferRequest` with `task_id` in `batch_id`.

- `batch_id`: The `BatchID` it belongs to;
- `task_id`: The sequence number of the `TransferRequest` to query;
- `status`: Output Transfer status;
- Return value: If successful, returns 0; otherwise, returns a negative value.

### Transport::freeBatchID

```cpp
int freeBatchID(BatchID batch_id);
```

Recycles `BatchID`, and subsequent operations on `submitTransfer` and `getTransferStatus` are undefined. If there are still `TransferRequest`s pending completion in the `BatchID`, the operation is refused.

- `batch_id`: The `BatchID` it belongs to;
- Return value: If successful, returns 0; otherwise, returns a negative value.

## Multi-Transport Management
The `TransferEngine` class internally manages multiple backend `Transport` classes, and users can load or unload `Transport` for different backends in `TransferEngine`.

### TransferEngine::installOrGetTransport
```cpp
Transport* installOrGetTransport(const std::string& proto, void** args);
```

Registers `Transport` in `TransferEngine`. If a `Transport` for a certain protocol already exists, it returns that `Transport`.

- `proto`: The name of the transport protocol used by `Transport`, currently supporting `tcp`, `rdma`, `nvmeof`.
- `args`: Additional parameters required for `Transport` initialization, presented as a variable-length array, with the last member being `nullptr`.
- Return value: If `proto` is within the determined range, returns the `Transport` corresponding to `proto`; otherwise, returns a null pointer.

#### TCP Transfer Mode
For TCP transfer mode, there is no need to pass `args` objects when registering the `Transport` object.
```cpp
engine->installOrGetTransport("tcp", nullptr);
```

#### RDMA Transfer Mode
For RDMA transfer mode, the network card priority marrix must be specified through `args` during the registration of `Transport`.
```cpp
void** args = (void**) malloc(2 * sizeof(void*));
args[0] = /* topology matrix */;
args[1] = nullptr;
engine->installOrGetTransport("rdma", args);
```
The network card priority marrix is a JSON string indicating the storage medium name and the list of network cards to be used preferentially, as shown in the example below:
```json
{
    "cpu:0": [["mlx0", "mlx1"], ["mlx2", "mlx3"]],
    "cuda:0": [["mlx1", "mlx0"]],
    ...
}
```
Each `key` represents the device name corresponding to a CPU socket or a GPU device.
Each `value` is a tuple of (`preferred_nic_list`, `accessable_nic_list`), each of which is a list of NIC names.
- `preferred_nic_list` indicates the preferred NICs, such as NICs directly connected to the CPU rather than across NUMA, or NICs under the same PCIe Switch for GPUs.
- `accessable_nic_list` indicates NICs that are not preferred but can theoretically connect, used for fault retry scenarios.

#### NVMeOF Transfer Mode
For NVMeOF transfer mode, the file path must be specified through `args` during the registration of `Transport`.
```cpp
void** args = (void**) malloc(2 * sizeof(void*));
args[0] = /* topology matrix */;
args[1] = nullptr;
engine->installOrGetTransport("nvmeof", args);
```

### TransferEngine::uninstallTransport
```cpp
int uninstallTransport(const std::string& proto);
```

Unloads `Transport` from `TransferEngine`.
- `proto`: The name of the transport protocol used by `Transport`, currently supporting `rdma`, `nvmeof`.
- Return value: If successful, returns 0; otherwise, returns a negative value.

## Space Registration

For the RDMA transfer process, the source pointer `TransferRequest::source` must be registered in advance as an RDMA readable/writable Memory Region space, that is, included as part of the RAM Segment of the current process. Therefore, the following functions are needed:

### TransferEngine::registerLocalMemory

```cpp
int registerLocalMemory(void *addr, size_t size, string location, bool remote_accessible);
```

Registers a space starting at address `addr` with a length of `size` on the local DRAM/VRAM.

- `addr`: The starting address of the registration space;
- `size`: The length of the registration space;
- `location`: The `device` corresponding to this memory segment, such as `cuda
:0` indicating the GPU device, `cpu:0` indicating the CPU socket, by matching with the network card priority order table (see `installOrGetTransport`), the preferred network card is identified.
- `remote_accessible`: Indicates whether this memory can be accessed by remote nodes.
- Return value: If successful, returns 0; otherwise, returns a negative value.

### TransferEngine::unregisterLocalMemory

```cpp
int unregisterLocalMemory(void *addr);
```

Unregisters the region.

- `addr`: The starting address of the registration space;
- Return value: If successful, returns 0; otherwise, returns a negative value.

## Segment Management and etcd Metadata

TransferEngine provides the `openSegment` function, which obtains a `SegmentHandle` for subsequent `Transport` transfers.
```cpp
SegmentHandle openSegment(const std::string& segment_name);
```

- `segment_name`: The unique identifier of the segment. For RAM Segment, this needs to be consistent with the `server_name` filled in by the peer process when initializing the TransferEngine object.
- Return value: If successful, returns the corresponding `SegmentHandle`; otherwise, returns a negative value.

```cpp
int closeSegment(SegmentHandle segment_id);
```

- `segment_id`: The unique identifier of the segment.
- Return value: If successful, returns 0; otherwise, returns a negative value.

<details>
<summary><strong>etcd Metadata Form</strong></summary>

```
// Used to find the communicable address and exposed rpc port based on server_name.
// Created: when calling TransferEngine::init().
// Deleted: when TransferEngine is destructed.
Key = mooncake/rpc_meta/[server_name]
Value = {
    'ip_or_host_name': 'node01'
    'rpc_port': 12345
}

// For segments, the key naming method of mooncake/[proto]/[segment_name] is used, and the segment name can use the Server Name.
// A segment corresponds to a machine, and a buffer corresponds to different segments of memory or different files or different disks on the machine. Different buffers of the same segment are in the same fault domain.

// RAM Segment, used by RDMA Transport to obtain transfer information.
// Created: command line tool register.py, at this time buffers are empty, only fill in the information that can be known in advance.
// Modified: TransferEngine at runtime through register / unregister to add or delete Buffer.
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
            'rkey': [1fe000, 1fdf00, ...], // The length is the same as the number of elements in the 'devices' field
        },
    ],
}

// Created: command line tool register.py, determine the file path that can be mounted.
// Modified: command line tool mount.py, add a mapping of the machine mounting the file to the file path on the mounting machine to the buffers.local_path_map.
Key = mooncake/nvmeof/[segment_name]
Value = {
    'server_name': server_name,
    'protocol': nvmeof,
    'buffers':[ 
    {
        'length': 1073741824,
        'file_path': "/mnt/nvme0" // The file path on this machine
        'local_path_map': {
            "node01": "/mnt/transfer_engine/node01/nvme0", // The machine mounting the file -> The file path on the mounting machine
            .....
        },
     }，
     {
        'length': 1073741824,
        'file_path': "/mnt/nvme1", 
        'local_path_map': {
            "node02": "/mnt/transfer_engine/node02/nvme1",
            .....
        },
     }
    ]
}
```
</details>

## Constructor and Initialization

```cpp
TransferEngine(std::unique_ptr<TransferMetadata> metadata_client);
TransferMetadata(const std::string &metadata_server);
```

- Pointer to a `TransferMetadata` object, which abstracts the communication logic between the TransferEngine framework and the metadata server/etcd, facilitating user deployment in different environments. `metadata_server` represents the IP address or hostname of the etcd server.

For easy exception handling, TransferEngine needs to call the init function for secondary construction after construction:
```cpp
int init(std::string& server_name, std::string& connectable_name, uint64_t rpc_port = 12345);
```

- `server_name`: The local server name, ensuring uniqueness within the cluster. It also serves as the name of the RAM Segment that other nodes refer to the current instance (i.e., Segment Name).
- `connectable_name`: The name used for other clients to connect, which can be a hostname or IP address.
- `rpc_port`: The rpc port used for interaction with other clients.
- Return value: If successful, returns 0; if TransferEngine has already been init, returns -1.

```cpp
  ~TransferEngine();
```

Reclaims all allocated resources and also deletes the global meta data server information.

## Example Program
`mooncake-transfer-engine/example/transfer_engine_bench.cpp` provides an example program that demonstrates the basic usage of Transfer Engine by initiating nodes to repeatedly read/write data blocks from the DRAM of the target node, and can be used to measure read/write throughput. Currently, the Transfer Engine Bench tool can be used for RDMA and TCP protocols.

## Using Transfer Engine to Your Projects
### Using C/C++ Interface
After compiling Mooncake Store, you can move the compiled static library file `libtransfer_engine.a` and the C header file `transfer_engine_c.h` into your own project. There is no need to reference other files under `src/transfer_engine`.

During the project build phase, you need to configure the following options for your application:
```bash
-I/path/to/include
-L/path/to/lib -ltransfer_engine
-lnuma -lglog -libverbs -ljsoncpp -letcd-cpp-api -lprotobuf -lgrpc++ -lgrpc
```

### Using Golang Interface
To support the operational needs of P2P Store, Transfer Engine provides a Golang interface wrapper, see `mooncake-p2p-store/src/p2pstore/transfer_engine.go`.

When compiling the project, enable the `-DWITH_P2P_STORE=ON` option to compile the P2P Store example program at the same time.

### Using Rust Interface
Under `mooncake-transfer-engine/example/rust-example`, the Rust interface implementation of TransferEngine is provided, and a Rust version of the benchmark is implemented based on the interface, similar to [transfer_engine_bench.cpp](../../../mooncake-transfer-engine/example/transfer_engine_bench.cpp). To compile rust-example, you need to install the Rust SDK and add `-DWITH_RUST_EXAMPLE=ON` in the cmake command.

## Advanced Runtime Options
For advanced users, TransferEngine provides the following advanced runtime options, all of which can be passed in through **environment variables**.

- `MC_NUM_CQ_PER_CTX` The number of CQs created per device instance, default value 1
- `MC_NUM_COMP_CHANNELS_PER_CTX` The number of Completion Channel created per device instance, default value 1
- `MC_IB_PORT` The IB port number used per device instance, default value 1
- `MC_GID_INDEX` The GID index used per device instance, default value 3 (or the maximum value supported by the platform)
- `MC_MAX_CQE_PER_CTX` The CQ buffer size per device instance, default value 4096
- `MC_MAX_EP_PER_CTX` The maximum number of active EndPoint per device instance, default value 256
- `MC_NUM_QP_PER_EP` The number of QPs per EndPoint, the more the number, the better the fine-grained I/O performance, default value 2
- `MC_MAX_SGE` The maximum number of SGEs supported per QP, default value 4 (or the highest value supported by the platform)
- `MC_MAX_WR` The maximum number of Work Request supported per QP, default value 256 (or the highest value supported by the platform)
- `MC_MAX_INLINE` The maximum Inline write data volume (bytes) supported per QP, default value 64 (or the highest value supported by the platform)
- `MC_MTU` The MTU length used per device instance, can be 512, 1024, 2048, 4096, default value 4096 (or the maximum length supported by the platform)
- `MC_WORKERS_PER_CTX` The number of asynchronous worker threads corresponding to each device instance
- `MC_SLICE_SIZE` The segmentation granularity of user requests in Transfer Engine
- `MC_RETRY_CNT` The maximum number of retries in Transfer Engine
- `MC_VERBOSE` If this option is set, more detailed logs will be output during runtime

