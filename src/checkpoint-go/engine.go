package main

import (
	"errors"
	"log"
	"sync"
	"time"

	"go.etcd.io/etcd/clientv3"
)

const kCheckpointMetadataPrefix string = "moonshot/checkpoint/"

type MemoryRange struct {
	Addr   uint64
	Length uint64
}

type CheckpointEngine struct {
	metadataUri          string
	goldCheckpointMap    map[string]struct{}
	replicaCheckpointMap map[string]struct{}
	memoryRanges         []MemoryRange
	etcdClient           *clientv3.Client
	localSegmentName     string
	transferEngine       *TransferEngine
	mu                   sync.Mutex
}

func (engine *CheckpointEngine) hasGoldCheckpoint(name string) bool {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	_, exist := engine.goldCheckpointMap[name]
	return exist
}

func (engine *CheckpointEngine) hasReplicaCheckpoint(name string) bool {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	_, exist := engine.replicaCheckpointMap[name]
	return exist
}

func (engine *CheckpointEngine) addGoldCheckpoint(name string) {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	engine.goldCheckpointMap[name] = struct{}{}
}

func (engine *CheckpointEngine) addReplicaCheckpoint(name string) {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	engine.replicaCheckpointMap[name] = struct{}{}
}

func (engine *CheckpointEngine) deleteGoldCheckpoint(name string) {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	delete(engine.goldCheckpointMap, name)
}

func (engine *CheckpointEngine) deleteReplicaCheckpoint(name string) {
	engine.mu.Lock()
	defer engine.mu.Unlock()
	delete(engine.replicaCheckpointMap, name)
}

func NewCheckpointEngine(metadataUri string, localSegmentName string, nicPriorityMatrix string) (*CheckpointEngine, error) {
	etcdClient, err := clientv3.New(clientv3.Config{
		Endpoints:   []string{metadataUri},
		DialTimeout: 5 * time.Second,
	})

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
		metadataUri:          metadataUri,
		goldCheckpointMap:    make(map[string]struct{}),
		replicaCheckpointMap: make(map[string]struct{}),
		etcdClient:           etcdClient,
		localSegmentName:     localSegmentName,
		transferEngine:       transferEngine,
	}
	return engine, nil
}

func (engine *CheckpointEngine) Close() error {
	err := engine.etcdClient.Close()
	if err != nil {
		return err
	}
	err = engine.transferEngine.Close()
	if err != nil {
		return err
	}
	return nil
}

func (engine *CheckpointEngine) ToString() string {
	return "CheckpointEngine: " + engine.metadataUri
}

// 检查是否有重叠并注册内存
func (engine *CheckpointEngine) tryRegisterMemory(newRange MemoryRange) error {
	engine.mu.Lock()
	defer engine.mu.Unlock()

	var toRegister []MemoryRange // 使用切片来存储需要注册的范围

	for _, existing := range engine.memoryRanges {
		if newRange.Addr < existing.Addr+existing.Length && existing.Addr < newRange.Addr+newRange.Length {
			// 有重叠，计算未重叠部分
			if newRange.Addr < existing.Addr { // 新范围在旧范围左边
				toRegister = append(toRegister, MemoryRange{Addr: newRange.Addr, Length: existing.Addr - newRange.Addr})
			}
			if newRange.Addr+newRange.Length > existing.Addr+existing.Length { // 新范围在旧范围右边
				toRegister = append(toRegister, MemoryRange{Addr: existing.Addr + existing.Length, Length: newRange.Addr + newRange.Length - (existing.Addr + existing.Length)})
			}
		}
	}

	// 注册新范围中不存在的部分
	for _, r := range toRegister {
		if r.Length > 0 {
			err := engine.transferEngine.registerLocalMemory(uintptr(r.Addr), r.Length, "cpu:0")
			if err != nil {
				return err
			}
		}
	}

	// 最后注册整个新范围
	// 检查新范围是否已经完全被覆盖
	alreadyRegistered := false
	for _, existing := range engine.memoryRanges {
		if newRange.Addr >= existing.Addr && newRange.Addr+newRange.Length <= existing.Addr+existing.Length {
			alreadyRegistered = true
			break
		}
	}

	if !alreadyRegistered {
		err := engine.transferEngine.registerLocalMemory(uintptr(newRange.Addr), newRange.Length, "cpu:0")
		if err != nil {
			return err
		}
	}

	engine.memoryRanges = append(engine.memoryRanges, newRange) // 更新已注册的范围
	return nil
}

func (engine *CheckpointEngine) RegisterCheckpoint(name string, addrList []uintptr, sizeList []uint64, shardSize uint64) error {
	addrListLen := len(addrList)
	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if engine.hasGoldCheckpoint(name) || engine.hasReplicaCheckpoint(name) {
		return errors.New("has gold and/or replica checkpoint")
	}

	var checkpoint Checkpoint
	checkpoint.Name = name
	checkpoint.ShardSize = shardSize
	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		memoryRange := MemoryRange{
			Addr:   uint64(addr),
			Length: size,
		}
		err := engine.tryRegisterMemory(memoryRange)
		if err != nil {
			return err
		}
		var offset uint64 = 0
		for ; offset < size; offset += shardSize {
			shardLength := shardSize
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

	err := engine.forcePutCheckpointMetadata(name, &checkpoint)
	if err != nil {
		return err
	}

	engine.addGoldCheckpoint(name)
	return nil
}

func (engine *CheckpointEngine) UnregisterCheckpoint(name string) error {
	engine.deleteGoldCheckpoint(name)

	for {
		checkpoint, revision, err := engine.getCheckpointMetadata(name)
		if err != nil {
			return err
		}

		if checkpoint == nil {
			return errors.New("checkpoint not exist in etcd")
		}

		for index := range checkpoint.Shards {
			checkpoint.Shards[index].Gold = nil
		}

		success, err := engine.updateCheckpointMetadata(name, checkpoint, revision)
		if err != nil {
			return err
		}

		if success {
			return nil
		}
	}
}

type CheckpointInfo struct {
	Name      string
	ShardSize uint64
	TotalSize uint64
	SizeList  []uint64
}

func (engine *CheckpointEngine) GetCheckpointInfo(namePrefix string) ([]CheckpointInfo, error) {
	var result []CheckpointInfo
	checkpoints, err := engine.listCheckpointMetadata(namePrefix)
	if err != nil {
		return result, err
	}
	for _, checkpoint := range checkpoints {
		checkpointInfo := CheckpointInfo{
			Name:      checkpoint.Name,
			TotalSize: checkpoint.Size,
			ShardSize: checkpoint.ShardSize,
		}
		for _, shard := range checkpoint.Shards {
			checkpointInfo.SizeList = append(checkpointInfo.SizeList, shard.Size)
			checkpointInfo.TotalSize += shard.Size
		}

		result = append(result, checkpointInfo)
	}
	return result, nil
}

func (engine *CheckpointEngine) GetLocalCheckpoint(name string, addrList []uintptr, sizeList []uint64) error {
	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}

	addrListLen := len(addrList)
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if engine.hasGoldCheckpoint(name) || engine.hasReplicaCheckpoint(name) {
		return errors.New("has gold and/or replica checkpoint")
	}

	checkpoint, revision, err := engine.getCheckpointMetadata(name)
	if err != nil {
		return err
	}

	if checkpoint == nil {
		return errors.New("checkpoint not exist in etcd")
	}

	batchSize := len(checkpoint.Shards)
	taskID := 0
	shardSize := checkpoint.ShardSize
	var requests []TransferRequest

	batchID, err := engine.transferEngine.allocateBatchID(batchSize)
	if err != nil {
		return err
	}

	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		memoryRange := MemoryRange{
			Addr:   uint64(addr),
			Length: size,
		}
		err := engine.tryRegisterMemory(memoryRange)
		if err != nil {
			return err
		}
		var offset uint64 = 0
		for ; offset < size; offset += shardSize {
			shard := checkpoint.Shards[taskID]
			location, err := shard.GetRandomLocation()
			if err != nil {
				return err
			}
			targetID, err := engine.transferEngine.getSegmentID(location.SegmentName)
			if err != nil {
				return err
			}
			request := TransferRequest{
				Opcode:       OPCODE_READ,
				Source:       uint64(addr) + offset,
				TargetID:     targetID,
				TargetOffset: location.Offset,
				Length:       shard.Size,
			}
			requests = append(requests, request)
			taskID++
		}
	}

	err = engine.transferEngine.submitTransfer(batchID, requests)
	if err != nil {
		return err
	}

	var retryTaskID []int
	for taskID := 0; taskID < batchSize; taskID++ {
		for {
			status, _, err := engine.transferEngine.getTransferStatus(batchID, taskID)
			if err != nil {
				return err
			}
			if status == STATUS_FAILED {
				retryTaskID = append(retryTaskID, taskID)
				break
			}
			if status == STATUS_COMPLETED {
				break
			}
		}
	}

	err = engine.transferEngine.freeBatchID(batchID)
	if err != nil {
		return err
	}

	for _, task_id := range retryTaskID {
		success := false
		for retryTimes := 0; !success && retryTimes < 4; retryTimes++ {
			request := requests[task_id]
			shard := checkpoint.Shards[taskID]
			location, err := shard.GetLocationWithRetries(retryTimes)
			if err != nil {
				return err
			}
			targetID, err := engine.transferEngine.getSegmentID(location.SegmentName)
			if err != nil {
				return err
			}
			request.TargetID = targetID
			request.TargetOffset = location.Offset
			batchID, err := engine.transferEngine.allocateBatchID(1)
			if err != nil {
				return err
			}
			err = engine.transferEngine.submitTransfer(batchID, []TransferRequest{request})
			if err != nil {
				return err
			}
			for {
				status, _, err := engine.transferEngine.getTransferStatus(batchID, 0)
				if err != nil {
					return err
				}
				if status == STATUS_FAILED {
					break
				}
				if status == STATUS_COMPLETED {
					success = true
					break
				}
			}
			err = engine.transferEngine.freeBatchID(batchID)
			if err != nil {
				return err
			}
		}
		if !success {
			return errors.New("transfer failed")
		}
	}

	return engine.finalizeGetLocalCheckpoint(name, addrList, sizeList, checkpoint, revision)
}

func (engine *CheckpointEngine) finalizeGetLocalCheckpoint(name string, addrList []uintptr, sizeList []uint64, checkpoint *Checkpoint, revision int64) error {
	for {
		taskID := 0
		shardSize := checkpoint.ShardSize
		for i := 0; i < len(addrList); i++ {
			addr, size := addrList[i], sizeList[i]
			var offset uint64 = 0
			for ; offset < size; offset += shardSize {
				replicaLocation := Location{
					SegmentName: engine.localSegmentName,
					Offset:      uint64(addr) + offset,
				}
				checkpoint.Shards[taskID].ReplicaList = append(checkpoint.Shards[taskID].ReplicaList, replicaLocation)
				taskID++
			}
		}

		success, err := engine.updateCheckpointMetadata(name, checkpoint, revision)
		if err != nil {
			return err
		}
		if success {
			return nil
		} else {
			checkpoint, revision, err = engine.getCheckpointMetadata(name)
			if err != nil {
				return err
			}

			if checkpoint == nil {
				return errors.New("checkpoint not exist in etcd")
			}
		}
	}
}

func (engine *CheckpointEngine) DeleteLocalCheckpoint(name string) error {
	engine.deleteReplicaCheckpoint(name)

	for {
		checkpoint, revision, err := engine.getCheckpointMetadata(name)
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
		success, err := engine.updateCheckpointMetadata(name, checkpoint, revision)
		if err != nil {
			return err
		}
		if success {
			return nil
		}
	}
}
