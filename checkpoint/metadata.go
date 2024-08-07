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
// 	max_shard_size: 128MB
//  size_list: [X, X, X,...]
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
// 	],
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
	Name         string   `json:"name"`
	Size         uint64   `json:"size"`
	SizeList     []uint64 `json:"size_list"`
	MaxShardSize uint64   `json:"max_shard_size"`
	Shards       []Shard  `json:"shards"`
}

func (s *Shard) GetLocation(retryTimes int) *Location {
	if retryTimes == 0 {
		return s.getRandomLocation()
	} else {
		return s.getRetryLocation(retryTimes - 1)
	}
}

func (s *Shard) getRandomLocation() *Location {
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	if len(s.ReplicaList) > 0 {
		index := r.Intn(len(s.ReplicaList))
		return &s.ReplicaList[index]
	} else if len(s.Gold) > 0 {
		index := r.Intn(len(s.Gold))
		return &s.Gold[index]
	}
	return nil
}

func (s *Shard) getRetryLocation(retryTimes int) *Location {
	if len(s.ReplicaList) > retryTimes {
		return &s.ReplicaList[retryTimes]
	}
	retryTimes -= len(s.ReplicaList)
	if len(s.Gold) > retryTimes {
		return &s.Gold[retryTimes]
	}
	return nil
}

func (s *Checkpoint) IsEmpty() bool {
	for _, shard := range s.Shards {
		if len(shard.Gold) != 0 || len(shard.ReplicaList) != 0 {
			return false
		}
	}
	return true
}

type Metadata struct {
	etcdClient *clientv3.Client
}

func NewMetadata(metadataUri string) (*Metadata, error) {
	etcdClient, err := clientv3.New(clientv3.Config{
		Endpoints:   []string{metadataUri},
		DialTimeout: 5 * time.Second,
	})

	if err != nil {
		return nil, err
	}

	return &Metadata{etcdClient: etcdClient}, nil
}

func (metadata *Metadata) Close() error {
	return metadata.etcdClient.Close()
}

func (metadata *Metadata) Create(name string, checkpoint *Checkpoint) error {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return err
	}
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	txnResp, err := metadata.etcdClient.Txn(ctx).
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

func (metadata *Metadata) Put(name string, checkpoint *Checkpoint) error {
	jsonData, err := json.Marshal(checkpoint)
	if err != nil {
		return err
	}
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_, err = metadata.etcdClient.Put(ctx, key, string(jsonData))
	return err
}

func (metadata *Metadata) Update(name string, checkpoint *Checkpoint, revision int64) (bool, error) {
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if checkpoint.IsEmpty() {
		txnResp, err := metadata.etcdClient.Txn(ctx).
			If(clientv3.Compare(clientv3.ModRevision(key), "=", revision)).
			Then(clientv3.OpDelete(key)).
			Commit()
		if err != nil {
			return false, err
		}
		return txnResp.Succeeded, nil
	} else {
		jsonData, err := json.Marshal(checkpoint)
		if err != nil {
			return false, err
		}
		txnResp, err := metadata.etcdClient.Txn(ctx).
			If(clientv3.Compare(clientv3.ModRevision(key), "=", revision)).
			Then(clientv3.OpPut(key, string(jsonData))).
			Commit()
		if err != nil {
			return false, err
		}
		return txnResp.Succeeded, nil
	}
}

func (metadata *Metadata) Get(name string) (*Checkpoint, int64, error) {
	key := kCheckpointMetadataPrefix + name
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	response, err := metadata.etcdClient.Get(ctx, key)
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

func (metadata *Metadata) List(namePrefix string) ([]*Checkpoint, error) {
	startRange := kCheckpointMetadataPrefix + namePrefix
	stopRange := kCheckpointMetadataPrefix + namePrefix + string([]byte{0xFF})
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	response, err := metadata.etcdClient.Get(ctx, startRange, clientv3.WithRange(stopRange))
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
