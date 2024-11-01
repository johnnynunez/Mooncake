# Mooncake P2P Store API

P2P Store 基于 [Transfer Engine](transfer-engine.md) 构建，支持在集群中的对等节点之间临时共享对象，典型的场景包括 Checkpoint 分发等。P2P Store 是纯客户端架构，没有统一的 Master 节点，全局元数据由 etcd 服务维护。P2P Store 现已用于 Moonshot AI 的检查点传输服务。

Mooncake P2P Store 目前基于 Golang 实现了下列接口：

```go
func NewP2PStore(metadataUri string, localSegmentName string, nicPriorityMatrix string) (*P2PStore, error)
```
创建 P2PStore 实例，该实例内部会启动一个 Transfer Engine 服务。
- `metadataUri`：元数据服务器/etcd 服务所在主机名或 IP 地址。
- `localSegmentName`：本地的 server name，保证在集群内唯一。
- `nicPriorityMatrix`：网卡优先级顺序表，参见位于 Transfer Engine 文档的相关描述（`TransferEngine::installOrGetTransport`）。
- 返回值：若成功则返回 `P2PStore` 实例指针，否则返回 `error`。

```go
func (store *P2PStore) Close() error
```
关闭 P2PStore 实例。

```go
type Buffer struct {
	addr uintptr
	size uint64
}

func (store *P2PStore) Register(ctx context.Context, name string, addrList []uintptr, sizeList []uint64, maxShardSize uint64, location string) error
```
注册本地某个文件到整个集群，使得整个集群可下载此文件。在调用 Unregister 之前需保证对应地址区间的数据不被修改或 unmap。
- `ctx`：Golang Context 引用。
- `name`：文件注册名，保证在集群内唯一。
- `addrList` 和 `sizeList`：这两个数组分别表示文件的内存范围，`addrList` 表示起始地址，`sizeList` 表示对应的长度。文件内容在逻辑上与数组中的顺序相对应。
- `maxShardSize`：内部数据切分粒度，推荐值 64MB。
- `location`：这一段内存对应的设备名称，与 `nicPriorityMatrix` 匹配。


```go
func (store *P2PStore) Unregister(ctx context.Context, name string) error
```
停止本地某个文件注册到整个集群。
- `ctx`：Golang Context 引用。
- `name`：文件注册名，保证在集群内唯一。

```go
type PayloadInfo struct {
	Name         string   // Checkpoint 文件的完整名称
	MaxShardSize uint64   // Register 传入的 maxShardSize
	TotalSize    uint64   // Register 传入的 sizeList 累加起来的总长度
	SizeList     []uint64 // Register 传入的 sizeList
}

func (store *P2PStore) List(ctx context.Context, namePrefix string) ([]PayloadInfo, error)
```
获取集群中已注册的文件列表，可使用文件名前缀进行过滤。
- `ctx`：Golang Context 引用。
- `namePrefix`：文件名前缀，若空则表示枚举所有文件。


```go
func (store *P2PStore) GetReplica(ctx context.Context, name string, addrList []uintptr, sizeList []uint64) error
```
拉取文件的一个副本到指定的本地内存区域，同时允许其他节点以此副本为来源拉取文件。在调用 DeleteReplica 之前需保证对应地址区间的数据不被修改或 unmap。一个文件在同一个 P2PStore 实例上只能拉取一次。
- `ctx`：Golang Context 引用。
- `name`：文件注册名，保证在集群内唯一。
- `addrList` 和 `sizeList`：这两个数组分别表示文件的内存范围，`addrList` 表示起始地址，`sizeList` 表示对应的长度。文件内容在逻辑上与数组中的顺序相对应。

```go
func (store *P2PStore) DeleteReplica(ctx context.Context, name string) error
```
停止其他节点从本地拉取文件。
- `ctx`：Golang Context 引用。
- `name`：文件注册名，保证在集群内唯一。
