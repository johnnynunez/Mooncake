package main

import (
	"context"
	"encoding/json"
	"fmt"
	"math/rand"
	"time"

	"go.etcd.io/etcd/clientv3"
)

// key = ckpt_name/xxx
// value = {
// 	size: 1GB
// 	shard_size: 128MB
// 	shards: [
// 	{
// 		size: addrSize[0]
// 		gold: [{ segment_name: 'megatron_segment_name', offset: addrList[0]}]
// 		replica_list: [{segment_name: X, offset: Y}, {segment_name: X, offset: Y}]  // 高u
// 	}
// 	{
// 		size: addrSize[1]
// 		gold: [{ segment_name: 'megatron_segment_name', offset: addrList[1]}]
// 		replica_list: [{segment_name: X, offset: Y}, {segment_name: X, offset: Y}]
// 	}
// 	]
// };

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
	MaxShardSize uint64  `json:"max_shard_size"`
	Shards    []Shard `json:"shards"`
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

func (s *Shard) GetLocationWithRetries(retryTimes int) (*Location, error) {
	if len(s.ReplicaList) > retryTimes {
		return &s.ReplicaList[retryTimes], nil
	}
	retryTimes -= len(s.ReplicaList)
	if len(s.Gold) > retryTimes {
		return &s.Gold[retryTimes], nil
	}
	return nil, fmt.Errorf("no locations available")
}

func (engine *CheckpointEngine) putCheckpointMetadata(name string, checkpoint *Checkpoint) error {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return err
	}
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	txnResp, err := engine.etcdClient.Txn(ctx).
		If(clientv3.Compare(clientv3.Version(key), "=", 0)).
		Then(clientv3.OpPut(key, string(jsonData))).
		Commit()
	if err != nil {
		return err
	}
	if !txnResp.Succeeded {
		return fmt.Errorf("key '%s' already exists", key)
	}
	return nil
}

func (engine *CheckpointEngine) forcePutCheckpointMetadata(name string, checkpoint *Checkpoint) error {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return err
	}
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_, err = engine.etcdClient.Put(ctx, key, string(jsonData))
	return err
}

func (engine *CheckpointEngine) updateCheckpointMetadata(name string, checkpoint *Checkpoint, revision int64) (bool, error) {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return false, err
	}
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	txnResp, err := engine.etcdClient.Txn(ctx).
		If(clientv3.Compare(clientv3.ModRevision(key), "=", revision)).
		Then(clientv3.OpPut(key, string(jsonData))).
		Commit()
	if err != nil {
		return false, err
	}
	return txnResp.Succeeded, nil
}

// 问题：如果传入的 etcd uri 关闭了，engine.etcdClient.Get 会被永久性阻塞。为何？
func (engine *CheckpointEngine) getCheckpointMetadata(name string) (*Checkpoint, int64, error) {
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	response, err := engine.etcdClient.Get(ctx, key)
	if err != nil {
		return nil, -1, err
	}
	if len(response.Kvs) > 0 {
		var retrievedCheckpoint Checkpoint
		err = json.Unmarshal(response.Kvs[0].Value, &retrievedCheckpoint)
		if err != nil {
			return nil, -1, err
		}
		return &retrievedCheckpoint, response.Kvs[0].ModRevision, nil
	}
	return nil, -1, nil
}

func (engine *CheckpointEngine) listCheckpointMetadata(namePrefix string) ([]*Checkpoint, error) {
	startRange := kCheckpointMetadataPrefix + namePrefix
	stopRange := kCheckpointMetadataPrefix + namePrefix + string([]byte{0xFF})
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	response, err := engine.etcdClient.Get(ctx, startRange, clientv3.WithRange(stopRange))
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
