package checkpoint

import (
	"context"
	"errors"
	"log"
	"sync"
)

const kCheckpointMetadataPrefix string = "moonshot/checkpoint/"

type CheckpointEngine struct {
	metadataUri      string
	localSegmentName string
	catalog          *Catalog
	memory           *RegisteredMemory
	metadata         *Metadata
	transfer         *TransferEngine
}

// 当用户传入 Checkpoint 粒度大于 MAX_CHUNK_SIZE 字节时，可在内部自动拆分成 Buffer 并行注册，从而提高内存注册操作效率
// 警告：内存注册操作是慢速操作
// MAX_CHUNK_SIZE 参数必须是 2 的整数幂，接口参数 maxShardSize 也必须是 2 的整数幂，并可整除 MAX_CHUNK_SIZE
// 即 MAX_CHUNK_SIZE % maxShardSize == 0
const MAX_CHUNK_SIZE uint64 = 4096 * 1024 * 1024

// Errors
var ErrInvalidArgument = errors.New("error: invalid argument")
var ErrAddressOverlapped = errors.New("error: address overlapped")
var ErrCheckpointOpened = errors.New("error: checkpoint opened in local")
var ErrCheckpointClosed = errors.New("error: checkpoint closed in local")
var ErrCheckpointNotFound = errors.New("error: checkpoint not found in cluster")
var ErrTooManyRetries = errors.New("error: too many retries")

func NewCheckpointEngine(metadataUri string, localSegmentName string, nicPriorityMatrix string) (*CheckpointEngine, error) {
	metadata, err := NewMetadata(metadataUri)
	if err != nil {
		return nil, err
	}

	transferEngine, err := NewTransferEngine(metadataUri, localSegmentName, nicPriorityMatrix)
	if err != nil {
		innerErr := metadata.Close()
		if innerErr != nil {
			log.Println("cascading error:", innerErr)
		}
		return nil, err
	}

	engine := &CheckpointEngine{
		metadataUri:      metadataUri,
		localSegmentName: localSegmentName,
		catalog:          NewCatalog(),
		memory:           NewRegisteredMemory(transferEngine, MAX_CHUNK_SIZE),
		metadata:         metadata,
		transfer:         transferEngine,
	}
	return engine, nil
}

func (engine *CheckpointEngine) Close() error {
	var retErr error = nil
	err := engine.transfer.Close()
	if err != nil {
		retErr = err
	}
	err = engine.metadata.Close()
	if err != nil {
		retErr = err
	}
	return retErr
}

type logicalMemoryRegion struct {
	addr uintptr
	size uint64
}

func (engine *CheckpointEngine) doUnregister(regionList []logicalMemoryRegion, maxShardSize uint64) {
	for _, region := range regionList {
		err := engine.memory.Remove(region.addr, region.size, maxShardSize)
		if err != nil {
			log.Println("cascading error:", err)
		}
	}
}

func (engine *CheckpointEngine) RegisterCheckpoint(
	ctx context.Context, name string,
	addrList []uintptr, sizeList []uint64, maxShardSize uint64) error {
	if len(addrList) != len(sizeList) || len(addrList) == 0 {
		return ErrInvalidArgument
	}

	if engine.catalog.Contains(name) {
		return ErrCheckpointOpened
	}

	var checkpoint Checkpoint
	var regionList []logicalMemoryRegion
	checkpoint.Name = name
	checkpoint.MaxShardSize = maxShardSize
	checkpoint.SizeList = sizeList
	for i := 0; i < len(addrList); i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.memory.Add(addr, size, maxShardSize)
		if err != nil {
			engine.doUnregister(regionList, maxShardSize)
			return err
		}
		regionList = append(regionList, logicalMemoryRegion{addr: addr, size: size})
		var offset uint64 = 0
		for ; offset < size; offset += maxShardSize {
			shardLength := maxShardSize
			if shardLength > size-offset {
				shardLength = size - offset
			}
			goldLocation := Location{
				SegmentName: engine.localSegmentName,
				Offset:      uint64(addr) + offset,
			}
			shard := Shard{
				Length:      shardLength,
				Gold:        []Location{goldLocation},
				ReplicaList: nil,
			}
			checkpoint.Shards = append(checkpoint.Shards, shard)
		}
	}

	err := engine.metadata.Put(ctx, name, &checkpoint)
	if err != nil {
		engine.doUnregister(regionList, maxShardSize)
		return err
	}

	params := CatalogParams{
		IsGold:       true,
		AddrList:     addrList,
		SizeList:     sizeList,
		MaxShardSize: maxShardSize,
	}
	engine.catalog.Add(name, params)
	return nil
}

func (engine *CheckpointEngine) UnregisterCheckpoint(ctx context.Context, name string) error {
	params, exist := engine.catalog.Get(name)
	if !exist {
		return ErrCheckpointClosed
	}

	for {
		checkpoint, revision, err := engine.metadata.Get(ctx, name)
		if err != nil {
			return err
		}

		if checkpoint == nil {
			return ErrCheckpointNotFound
		}

		for index := range checkpoint.Shards {
			checkpoint.Shards[index].Gold = nil
		}

		success, err := engine.metadata.Update(ctx, name, checkpoint, revision)
		if err != nil {
			return err
		}

		if success {
			engine.catalog.Remove(name)
			for index := 0; index < len(params.AddrList); index++ {
				innerErr := engine.memory.Remove(params.AddrList[index], params.SizeList[index], params.MaxShardSize)
				if innerErr != nil {
					log.Println("cascading error:", innerErr)
				}
			}
			return nil
		}
	}
}

type CheckpointInfo struct {
	Name         string   // Checkpoint 文件的完整名称
	MaxShardSize uint64   // RegisterCheckpoint 传入的 maxShardSize
	TotalSize    uint64   // RegisterCheckpoint 传入的 sizeList 累加起来的总长度
	SizeList     []uint64 // RegisterCheckpoint 传入的 sizeList
}

func (engine *CheckpointEngine) GetCheckpointInfo(ctx context.Context, namePrefix string) ([]CheckpointInfo, error) {
	var result []CheckpointInfo
	checkpoints, err := engine.metadata.List(ctx, namePrefix)
	if err != nil {
		return result, err
	}
	for _, checkpoint := range checkpoints {
		checkpointInfo := CheckpointInfo{
			Name:         checkpoint.Name,
			TotalSize:    checkpoint.Size,
			MaxShardSize: checkpoint.MaxShardSize,
			SizeList:     checkpoint.SizeList,
		}
		result = append(result, checkpointInfo)
	}
	return result, nil
}

// 该接口不允许对相同的 name 调用两次，如果第二次调用会报告 ErrInvalidArgument 错误。
func (engine *CheckpointEngine) GetLocalCheckpoint(ctx context.Context, name string, addrList []uintptr, sizeList []uint64) error {
	if len(addrList) != len(sizeList) || len(addrList) == 0 {
		return ErrInvalidArgument
	}

	if engine.catalog.Contains(name) {
		return ErrCheckpointOpened
	}

	checkpoint, revision, err := engine.metadata.Get(ctx, name)
	if err != nil {
		return err
	}

	if checkpoint == nil {
		return ErrCheckpointNotFound
	}

	var wg sync.WaitGroup
	errChan := make(chan error, 1)

	var offset uint64 = 0
	taskID := 0
	maxShardSize := checkpoint.MaxShardSize

	for i := 0; i < len(addrList); i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.memory.Add(addr, size, maxShardSize)
		if err != nil {
			return err
		}
		for ; offset < size; offset += maxShardSize {
			source := addr + uintptr(offset)
			shard := checkpoint.Shards[taskID]
			taskID++
			wg.Add(1)
			go func() {
				defer wg.Done()
				err = engine.performTransfer(source, shard)
				if err != nil {
					select {
					case errChan <- err:
					default:
					}
				}
			}()
		}
	}

	wg.Wait()
	close(errChan)
	select {
	case err := <-errChan:
		if err != nil {
			return err
		}
	default:
	}

	return engine.finalizeGetLocalCheckpoint(ctx, name, addrList, sizeList, checkpoint, revision)
}

func (engine *CheckpointEngine) performTransfer(source uintptr, shard Shard) error {
	const MAX_RETRY_COUNT int = 8
	retryCount := 0

	for retryCount < MAX_RETRY_COUNT {
		batchID, err := engine.transfer.allocateBatchID(1)
		if err != nil {
			return err
		}

		location := shard.GetLocation(retryCount)
		if location == nil {
			break
		}

		targetID, err := engine.transfer.getSegmentID(location.SegmentName)
		if err != nil {
			return err
		}

		request := TransferRequest{
			Opcode:       OPCODE_READ,
			Source:       uint64(source),
			TargetID:     targetID,
			TargetOffset: location.Offset,
			Length:       shard.Length,
		}

		err = engine.transfer.submitTransfer(batchID, []TransferRequest{request})
		if err != nil {
			return err
		}

		var status int
		for status == STATUS_WAITING || status == STATUS_PENDING {
			status, _, err = engine.transfer.getTransferStatus(batchID, 0)
			if err != nil {
				return err
			}
		}

		err = engine.transfer.freeBatchID(batchID)
		if err != nil {
			return err
		}

		if status == STATUS_COMPLETED {
			return nil
		}

		retryCount++
	}

	return ErrTooManyRetries
}

func (engine *CheckpointEngine) finalizeGetLocalCheckpoint(ctx context.Context, name string, addrList []uintptr, sizeList []uint64, checkpoint *Checkpoint, revision int64) error {
	for {
		taskID := 0
		maxShardSize := checkpoint.MaxShardSize
		for i := 0; i < len(addrList); i++ {
			addr, size := addrList[i], sizeList[i]
			var offset uint64 = 0
			for ; offset < size; offset += maxShardSize {
				replicaLocation := Location{
					SegmentName: engine.localSegmentName,
					Offset:      uint64(addr) + offset,
				}
				checkpoint.Shards[taskID].ReplicaList = append(checkpoint.Shards[taskID].ReplicaList, replicaLocation)
				taskID++
			}
		}

		success, err := engine.metadata.Update(ctx, name, checkpoint, revision)
		if err != nil {
			return err
		}
		if success {
			params := CatalogParams{
				IsGold:       false,
				AddrList:     addrList,
				SizeList:     sizeList,
				MaxShardSize: maxShardSize,
			}
			engine.catalog.Add(name, params)
			return nil
		} else {
			checkpoint, revision, err = engine.metadata.Get(ctx, name)
			if err != nil {
				return err
			}

			if checkpoint == nil {
				return ErrCheckpointNotFound
			}
		}
	}
}

func (engine *CheckpointEngine) DeleteLocalCheckpoint(ctx context.Context, name string) error {
	params, exist := engine.catalog.Get(name)
	if !exist {
		return ErrCheckpointClosed
	}

	for {
		checkpoint, revision, err := engine.metadata.Get(ctx, name)
		if err != nil {
			return err
		}
		if checkpoint == nil {
			return ErrCheckpointNotFound
		}

		for _, shard := range checkpoint.Shards {
			var newReplicaList []Location
			for _, replica := range shard.ReplicaList {
				if replica.SegmentName != engine.localSegmentName {
					newReplicaList = append(newReplicaList, replica)
				}
			}
			shard.ReplicaList = newReplicaList
		}
		success, err := engine.metadata.Update(ctx, name, checkpoint, revision)
		if err != nil {
			return err
		}
		if success {
			engine.catalog.Remove(name)
			for index := 0; index < len(params.AddrList); index++ {
				innerErr := engine.memory.Remove(params.AddrList[index], params.SizeList[index], params.MaxShardSize)
				if innerErr != nil {
					log.Println("cascading error:", innerErr)
				}
			}
			return nil
		}
	}
}
