package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"math/rand"
	"sync"
	"time"

	"go.etcd.io/etcd/clientv3"
)

const kCheckpointMetadataPrefix string = "moonshot/checkpoint/"

/*
key = ckpt_name/xxx
value = {
	size: 1GB
	shard_size: 128MB
	shards: [
	{
		size: addrSize[0]
		gold: [{ segment_name: 'megatron_segment_name', offset: addrList[0]}]
		replica_list: [{segment_name: X, offset: Y}, {segment_name: X, offset: Y}]  // 高u
	}
	{
		size: addrSize[1]
		gold: [{ segment_name: 'megatron_segment_name', offset: addrList[1]}]
		replica_list: [{segment_name: X, offset: Y}, {segment_name: X, offset: Y}]
	}
	]
};
*/

type Location struct {
	SegmentName string `json:"segment_name"`
	Offset      uint64 `json:"offset"`
}

type Shard struct {
	Size        uint64     `json:"size"`
	Gold        []Location `json:"gold"`
	ReplicaList []Location `json:"replica_list"`
}

type Checkpoint struct {
	Name      string  `json:"name"`
	Size      uint64  `json:"size"`
	ShardSize uint64  `json:"shard_size"`
	Shards    []Shard `json:"shards"`
}

type CheckpointEngine struct {
	metadataUri          string
	goldCheckpointMap    map[string]Checkpoint
	replicaCheckpointMap map[string]Checkpoint
	etcdClient           *clientv3.Client
	localSegmentName     string
	transferEngine       *TransferEngine
	mu                   sync.Mutex
}

func (s *Shard) GetRandomLocation() (*Location, error) {
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	if len(s.ReplicaList) > 0 {
		index := r.Intn(len(s.ReplicaList))
		return &s.ReplicaList[index], nil
	} else if len(s.Gold) > 0 {
		index := r.Intn(len(s.Gold))
		return &s.Gold[index], nil
	}
	return nil, fmt.Errorf("no locations available")
}

func NewCheckpointEngine(metadataUri string, localSegmentName string) (*CheckpointEngine, error) {
	etcdClient, err := clientv3.New(clientv3.Config{
		Endpoints:   []string{metadataUri},
		DialTimeout: 5 * time.Second,
	})

	if err != nil {
		return nil, err
	}

	nicPriorityMatrix := "{ \"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]]}"
	transferEngine, err := NewTransferEngine(metadataUri, localSegmentName, nicPriorityMatrix)
	if err != nil {
		return nil, err
	}

	engine := &CheckpointEngine{
		metadataUri:          metadataUri,
		goldCheckpointMap:    make(map[string]Checkpoint),
		replicaCheckpointMap: make(map[string]Checkpoint),
		etcdClient:           etcdClient,
		localSegmentName:     localSegmentName,
		transferEngine:       transferEngine,
	}
	return engine, nil
}

func (engine *CheckpointEngine) Shutdown() error {
	err := engine.etcdClient.Close()
	if err != nil {
		return err
	}
	err = engine.transferEngine.Destroy()
	if err != nil {
		return err
	}
	return nil
}

func (engine *CheckpointEngine) ToString() string {
	return "CheckpointEngine: " + engine.metadataUri
}

func (engine *CheckpointEngine) RegisterCheckpoint(name string, addrList []uintptr, sizeList []uint64, shardSize uint64) error {
	engine.mu.Lock()
	defer engine.mu.Unlock()

	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}
	addrListLen := len(addrList)
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if _, exist := engine.goldCheckpointMap[name]; exist {
		return errors.New("checkpoint exist")
	}

	var checkpoint Checkpoint
	checkpoint.Name = name
	checkpoint.ShardSize = shardSize
	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.transferEngine.registerLocalMemory(addr, size, "cpu:0")
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

	engine.goldCheckpointMap[name] = checkpoint
	return engine.uploadCheckpointMetadata(name, &checkpoint)
}

func (engine *CheckpointEngine) UnregisterCheckpoint(name string) error {
	engine.mu.Lock()
	defer engine.mu.Unlock()

	if _, exist := engine.goldCheckpointMap[name]; !exist {
		return errors.New("checkpoint not exist")
	}
	delete(engine.goldCheckpointMap, name)

	checkpoint, err := engine.downloadCheckpointMetadata(name)
	if err != nil {
		return err
	}

	if checkpoint == nil {
		return errors.New("checkpoint not exist in etcd")
	}

	for index := range checkpoint.Shards {
		checkpoint.Shards[index].Gold = nil
	}

	return engine.uploadCheckpointMetadata(name, checkpoint)
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
	engine.mu.Lock()
	defer engine.mu.Unlock()

	if len(addrList) != len(sizeList) {
		return errors.New("addrList and sizeList must be equal")
	}
	addrListLen := len(addrList)
	if addrListLen == 0 {
		return errors.New("addrList is empty")
	}

	if _, exist := engine.goldCheckpointMap[name]; exist {
		return errors.New("checkpoint exist in goldCheckpointMap")
	}

	if _, exist := engine.replicaCheckpointMap[name]; exist {
		return errors.New("checkpoint exist in replicaCheckpointMap")
	}

	checkpoint, err := engine.downloadCheckpointMetadata(name)
	if err != nil {
		return err
	}
	if checkpoint == nil {
		return errors.New("checkpoint not exist in etcd")
	}

	engine.replicaCheckpointMap[name] = *checkpoint
	batchSize := len(checkpoint.Shards)

	batchID, err := engine.transferEngine.allocateBatchID(batchSize)
	if err != nil {
		return err
	}

	var requests []TransferRequest

	taskID := 0
	shardSize := checkpoint.ShardSize

	for i := 0; i < addrListLen; i++ {
		addr, size := addrList[i], sizeList[i]
		err := engine.transferEngine.registerLocalMemory(addr, size, "cpu:0")
		if err != nil {
			return err
		}
		var offset uint64 = 0
		for ; offset < size; offset += shardSize {
			replicaLocation := Location{
				SegmentName: engine.localSegmentName,
				Offset:      uint64(addr) + offset,
			}
			shard := checkpoint.Shards[taskID]
			location, _ := shard.GetRandomLocation()
			targetID, err := engine.transferEngine.getSegmentID(location.SegmentName)
			if err != nil {
				return err
			}
			request := TransferRequest{
				Opcode:       OPCODE_READ,
				Source:       replicaLocation.Offset,
				TargetID:     targetID,
				TargetOffset: location.Offset,
				Length:       shard.Size,
			}
			checkpoint.Shards[taskID].ReplicaList = append(checkpoint.Shards[taskID].ReplicaList, replicaLocation)
			requests = append(requests, request)
			taskID++
		}
	}

	err = engine.transferEngine.submitTransfer(batchID, requests)
	if err != nil {
		return err
	}

	for taskID := 0; taskID < batchSize; taskID++ {
		for {
			status, _, err := engine.transferEngine.getTransferStatus(batchID, taskID)
			if err != nil || status == STATUS_FAILED {
				return err
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

	return engine.uploadCheckpointMetadata(name, checkpoint)
}

func (engine *CheckpointEngine) DeleteLocalCheckpoint(name string) error {
	checkpoint, err := engine.downloadCheckpointMetadata(name)
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
	err = engine.uploadCheckpointMetadata(name, checkpoint)
	if err != nil {
		return err
	}
	return nil
}

func (engine *CheckpointEngine) uploadCheckpointMetadata(name string, checkpoint *Checkpoint) error {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return err
	}

	key := kCheckpointMetadataPrefix + name
	fmt.Println(string(jsonData))
	_, err = engine.etcdClient.Put(context.Background(), key, string(jsonData))
	if err != nil {
		return err
	}
	return nil
}

// 问题：如果传入的 etcd uri 关闭了，engine.etcdClient.Get 会被永久性阻塞。为何？
func (engine *CheckpointEngine) downloadCheckpointMetadata(name string) (*Checkpoint, error) {
	key := kCheckpointMetadataPrefix + name
	response, err := engine.etcdClient.Get(context.Background(), key)
	if err != nil {
		return nil, err
	}
	if len(response.Kvs) > 0 {
		var retrievedCheckpoint Checkpoint
		err = json.Unmarshal(response.Kvs[0].Value, &retrievedCheckpoint)
		if err != nil {
			return nil, err
		}
		return &retrievedCheckpoint, nil
	}
	return nil, nil
}

func (engine *CheckpointEngine) listCheckpointMetadata(namePrefix string) ([]*Checkpoint, error) {
	startRange := kCheckpointMetadataPrefix + namePrefix
	stopRange := kCheckpointMetadataPrefix + namePrefix + string([]byte{0xFF})
	response, err := engine.etcdClient.Get(context.Background(), startRange, clientv3.WithRange(stopRange))
	if err != nil {
		return nil, err
	}
	var checkpoints []*Checkpoint
	for _, record := range response.Kvs {
		var retrievedCheckpoint Checkpoint
		err = json.Unmarshal(record.Value, &retrievedCheckpoint)
		if err != nil {
			return nil, err
		}
		checkpoints = append(checkpoints, &retrievedCheckpoint)
	}
	return checkpoints, nil
}
