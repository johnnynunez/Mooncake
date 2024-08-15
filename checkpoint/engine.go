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
	registeredMemory *RegisteredMemory
	metadata         *Metadata
	transferEngine   *TransferEngine
}

func NewCheckpointEngine(metadataUri string, localSegmentName string, nicPriorityMatrix string) (*CheckpointEngine, error) {
	metadata, err := NewMetadata(metadataUri)
	if err != nil {
		log.Printf("failed to make checkpoint engine: %v", err)
		return nil, err
	}

	transferEngine, err := NewTransferEngine(metadataUri, localSegmentName, nicPriorityMatrix)
	if err != nil {
		log.Printf("failed to make checkpoint engine: %v", err)
		return nil, err
	}

	engine := &CheckpointEngine{
		metadataUri:      metadataUri,
		localSegmentName: localSegmentName,
		catalog:          NewCatalog(),
		registeredMemory: NewRegisteredMemory(transferEngine),
		metadata:         metadata,
		transferEngine:   transferEngine,
	}
	return engine, nil
}

func (engine *CheckpointEngine) Close() error {
	err := engine.transferEngine.Close()
	if err != nil {
		return err
	}
	err = engine.metadata.Close()
	if err != nil {
		return err
	}
	return nil
}

func (engine *CheckpointEngine) ToString() string {
	return "CheckpointEngine: " + engine.metadataUri
}

// 注册 Checkpoint Gold 副本
// [ ] 对相同 name 的各类操作应当进行有效的序列化
// [ ] Crash 后 etcd 已有的 KV Entry 应当怎么处理
// [X] 如果没有 Replica 及 Gold，etcd 已有的 KV Entry 会自动删除
func (engine *CheckpointEngine) RegisterCheckpoint(ctx context.Context, name string, addrList []uintptr, sizeList []uint64, maxShardSize uint64) error {
	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}

	addrListLen := len(addrList)
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if engine.catalog.Contains(name) {
		return errors.New("has gold or replica checkpoint")
	}

	var checkpoint Checkpoint
	checkpoint.Name = name
	checkpoint.MaxShardSize = maxShardSize
	checkpoint.SizeList = sizeList
	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.registeredMemory.Add(addr, size, maxShardSize)
		if err != nil {
			return err
		}
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
				Size:        shardLength,
				Gold:        []Location{goldLocation},
				ReplicaList: nil,
			}
			checkpoint.Shards = append(checkpoint.Shards, shard)
		}
	}

	err := engine.metadata.Put(ctx, name, &checkpoint)
	if err != nil {
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
		return errors.New("checkpoint seems not registered")
	}

	for {
		checkpoint, revision, err := engine.metadata.Get(ctx, name)
		if err != nil {
			return err
		}

		if checkpoint == nil {
			return errors.New("checkpoint not exist in etcd")
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
				err = engine.registeredMemory.Remove(params.AddrList[index], params.SizeList[index], params.MaxShardSize)
				if err != nil {
					return err
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

type ShardEntry struct {
	source uintptr
	shard  Shard
}

func (engine *CheckpointEngine) GetLocalCheckpoint(ctx context.Context, name string, addrList []uintptr, sizeList []uint64) error {
	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}

	addrListLen := len(addrList)
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if engine.catalog.Contains(name) {
		return errors.New("has gold and/or replica checkpoint")
	}

	checkpoint, revision, err := engine.metadata.Get(ctx, name)
	if err != nil {
		return err
	}

	if checkpoint == nil {
		return errors.New("checkpoint not exist in etcd")
	}

	var wg sync.WaitGroup
	var offset uint64 = 0
	var asyncError error
	taskID := 0
	maxShardSize := checkpoint.MaxShardSize
	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.registeredMemory.Add(addr, size, maxShardSize)
		if err != nil {
			return err
		}
		for ; offset < size; offset += maxShardSize {
			shardEntry := ShardEntry{
				source: addr + uintptr(offset),
				shard:  checkpoint.Shards[taskID],
			}
			taskID++
			wg.Add(1)
			go func() {
				defer wg.Done()
				err = engine.performTransfer(shardEntry)
				if err != nil {
					asyncError = err
				}
			}()
		}
	}

	wg.Wait()

	if asyncError != nil {
		return asyncError
	}

	return engine.finalizeGetLocalCheckpoint(ctx, name, addrList, sizeList, checkpoint, revision)
}

func (engine *CheckpointEngine) performTransfer(shardEntry ShardEntry) error {
	const MAX_RETRY_COUNT int = 8
	retryCount := 0

	for retryCount < MAX_RETRY_COUNT {
		batchID, err := engine.transferEngine.allocateBatchID(1)
		if err != nil {
			return err
		}

		location := shardEntry.shard.GetLocation(retryCount)
		if location == nil {
			break
		}

		targetID, err := engine.transferEngine.getSegmentID(location.SegmentName)
		if err != nil {
			return err
		}

		request := TransferRequest{
			Opcode:       OPCODE_READ,
			Source:       uint64(shardEntry.source),
			TargetID:     targetID,
			TargetOffset: location.Offset,
			Length:       shardEntry.shard.Size,
		}

		err = engine.transferEngine.submitTransfer(batchID, []TransferRequest{request})
		if err != nil {
			return err
		}

		var status int
		for status == STATUS_WAITING || status == STATUS_PENDING {
			status, _, err = engine.transferEngine.getTransferStatus(batchID, 0)
			if err != nil {
				return err
			}
		}

		err = engine.transferEngine.freeBatchID(batchID)
		if err != nil {
			return err
		}

		if status == STATUS_COMPLETED {
			return nil
		}

		retryCount++
	}

	return errors.New("retry times exceed the limit")
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
				return errors.New("checkpoint not exist in etcd")
			}
		}
	}
}

func (engine *CheckpointEngine) DeleteLocalCheckpoint(ctx context.Context, name string) error {
	params, exist := engine.catalog.Get(name)
	if !exist {
		return errors.New("checkpoint seems not registered")
	}

	for {
		checkpoint, revision, err := engine.metadata.Get(ctx, name)
		if err != nil {
			return err
		}
		if checkpoint == nil {
			return errors.New("checkpoint does not exist")
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
				err = engine.registeredMemory.Remove(params.AddrList[index], params.SizeList[index], params.MaxShardSize)
				if err != nil {
					return err
				}
			}
			return nil
		}
	}
}
