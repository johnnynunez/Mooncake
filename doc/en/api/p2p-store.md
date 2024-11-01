# Mooncake P2P Store API

P2P Store is built on [Transfer Engine](transfer-engine.md) and supports the temporary sharing of objects between peer nodes in a cluster, with typical scenarios including checkpoint distribution. P2P Store is a client-only architecture with no master node required, and global metadata is maintained by the etcd service. P2P Store is now used in Moonshot AI's checkpoint transfer service.

Mooncake P2P Store currently implements the following interfaces in Golang:

```go
func NewP2PStore(metadataUri string, localSegmentName string, nicPriorityMatrix string) (*P2PStore, error)
```
Creates an instance of P2PStore, which internally starts a Transfer Engine service.
- `metadataUri`: The hostname or IP address of the metadata server/etcd service.
- `localSegmentName`: The local server name (hostname/IP address:port), ensuring uniqueness within the cluster.
- `nicPriorityMatrix`: The network interface card priority order matrix, see the related description in the Transfer Engine API documentation (`TransferEngine::installOrGetTransport`).
- Return value: If successful, returns a pointer to the `P2PStore` instance, otherwise returns `error`.

```go
func (store *P2PStore) Close() error
```
Closes the P2PStore instance.

```go
type Buffer struct {
	addr uintptr
	size uint64
}

func (store *P2PStore) Register(ctx context.Context, name string, addrList []uintptr, sizeList []uint64, maxShardSize uint64, location string) error
```
Registers a local file to the cluster, making it downloadable by other peers. Ensure that the data in the specified address range is not modified or unmapped before calling `Unregister`.
- `ctx`: Golang Context reference.
- `name`: The file registration name, ensuring uniqueness within the cluster.
- `addrList` and `sizeList`: These two arrays represent the memory range of the file, with `addrList` indicating the starting address and `sizeList` indicating the corresponding length. The file content corresponds logically to the order in the arrays.
- `maxShardSize`: The internal data sharding granularity, with a recommended value of 64MB.
- `location`: The device name corresponding to this memory segment, matching with `nicPriorityMatrix`.

```go
func (store *P2PStore) Unregister(ctx context.Context, name string) error
```
Stops the registration of a local file to the entire cluster.
- `ctx`: Golang Context reference.
- `name`: The file registration name, ensuring uniqueness within the cluster.

```go
type PayloadInfo struct {
	Name         string   // Full name of the Checkpoint file
	MaxShardSize uint64   // The maxShardSize passed into Register
	TotalSize    uint64   // The total length of the sizeList passed into Register
	SizeList     []uint64 // The sizeList passed into Register
}

func (store *P2PStore) List(ctx context.Context, namePrefix string) ([]PayloadInfo, error)
```
Obtains a list of files registered in the cluster, with the ability to filter by file name prefix.
- `ctx`: Golang Context reference.
- `namePrefix`: The file name prefix; if empty, it indicates enumeration of all files.

```go
func (store *P2PStore) GetReplica(ctx context.Context, name string, addrList []uintptr, sizeList []uint64) error
```
Pulls a copy of a file to a specified local memory area, while allowing other nodes to pull the file from this copy. Ensure that the data in the corresponding address range is not modified or unmapped before calling DeleteReplica. A file can only be pulled once on the same P2PStore instance.
- `ctx`: Golang Context reference.
- `name`: The file registration name, ensuring uniqueness within the cluster.
- `addrList` and `sizeList`: These two arrays represent the memory range of the file, with `addrList` indicating the starting address and `sizeList` indicating the corresponding length. The file content corresponds logically to the order in the arrays.

```go
func (store *P2PStore) DeleteReplica(ctx context.Context, name string) error
```
Stops other nodes from pulling the file from the local node.
- `ctx`: Golang Context reference.
- `name`: The file registration name, ensuring uniqueness within the cluster.
