package p2pstore

import (
	"log"
	"sync"
)

type BufferHandle struct {
	addr     uintptr
	length   uint64
	refCount int
}

type RegisteredMemory struct {
	engine       *TransferEngine
	bufferList   []BufferHandle
	mu           sync.Mutex
	maxChunkSize uint64
}

func NewRegisteredMemory(transferEngine *TransferEngine, maxChunkSize uint64) *RegisteredMemory {
	return &RegisteredMemory{engine: transferEngine, maxChunkSize: maxChunkSize}
}

// 将地址 [addr, addr + length] 加入注册列表。如果地址已注册，则引用计数+1。暂不支持相交（但不一致）的内存区域注册
func (memory *RegisteredMemory) Add(addr uintptr, length uint64, maxShardSize uint64, location string) error {
	if memory.maxChunkSize == 0 || memory.maxChunkSize%maxShardSize != 0 {
		return ErrInvalidArgument
	}

	memory.mu.Lock()
	for idx, entry := range memory.bufferList {
		if entry.addr == addr && entry.length == length {
			memory.bufferList[idx].refCount++
			memory.mu.Unlock()
			return nil
		}

		entryEndAddr := entry.addr + uintptr(entry.length)
		requestEndAddr := addr + uintptr(length)
		if addr < entryEndAddr && requestEndAddr > entry.addr {
			memory.mu.Unlock()
			return ErrAddressOverlapped
		}
	}
	memory.bufferList = append(memory.bufferList,
		BufferHandle{addr: addr, length: length, refCount: 1})
	memory.mu.Unlock()

	// Proceed memory registration
	var wg sync.WaitGroup
	errChan := make(chan error, 1)
	successfulTasks := make([]uintptr, 0)
	mu := &sync.Mutex{}

	for offset := uint64(0); offset < length; offset += memory.maxChunkSize {
		chunkSize := memory.maxChunkSize
		if chunkSize > length-offset {
			chunkSize = length - offset
		}

		wg.Add(1)
		go func(offset, chunkSize uint64) {
			defer wg.Done()
			baseAddr := addr + uintptr(offset)
			err := memory.engine.registerLocalMemory(baseAddr, chunkSize, location)
			if err != nil {
				select {
				case errChan <- err:
					close(errChan)
					return
				default:
				}
			} else {
				mu.Lock()
				successfulTasks = append(successfulTasks, baseAddr)
				mu.Unlock()
			}
		}(offset, chunkSize)
	}

	wg.Wait()
	close(errChan)

	if err := <-errChan; err != nil {
		for _, baseAddr := range successfulTasks {
			unregisterErr := memory.engine.unregisterLocalMemory(baseAddr)
			if unregisterErr != nil {
				log.Println("cascading error:", unregisterErr)
			}
		}
		return err
	}

	return nil
}

func (memory *RegisteredMemory) Remove(addr uintptr, length uint64, maxShardSize uint64) error {
	if memory.maxChunkSize == 0 || memory.maxChunkSize%maxShardSize != 0 {
		return ErrInvalidArgument
	}

	memory.mu.Lock()
	found := false
	for idx, entry := range memory.bufferList {
		if entry.addr == addr && entry.length == length {
			found = true
			entry.refCount--
			if entry.refCount == 0 {
				memory.bufferList = append(memory.bufferList[:idx],
					memory.bufferList[idx+1:]...)
				break
			}
		}
	}
	memory.mu.Unlock()
	if !found {
		return ErrInvalidArgument
	}

	var wg sync.WaitGroup
	errChan := make(chan error, 1)
	for offset := uint64(0); offset < length; offset += memory.maxChunkSize {
		wg.Add(1)
		go func(offset uint64) {
			defer wg.Done()
			err := memory.engine.unregisterLocalMemory(addr + uintptr(offset))
			if err != nil {
				select {
				case errChan <- err:
				default:
				}
			}
		}(offset)
	}

	wg.Wait()
	close(errChan)
	select {
	case err := <-errChan:
		return err
	default:
	}
	return nil
}
