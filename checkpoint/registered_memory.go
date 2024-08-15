package checkpoint

import (
	"errors"
	"sync"
)

type memoryRegion struct {
	addr     uintptr
	length   uint64
	refCount int
}

type RegisteredMemory struct {
	transferEngine   *TransferEngine
	memoryRegionList []memoryRegion
	mu               sync.Mutex
}

func NewRegisteredMemory(transferEngine *TransferEngine) *RegisteredMemory {
	return &RegisteredMemory{transferEngine: transferEngine}
}

const maxChunkSize uint64 = 4096 * 1024 * 1024

func (memory *RegisteredMemory) Add(addr uintptr, length uint64, maxShardSize uint64) error {
	var wg sync.WaitGroup
	var asyncError error
	if maxChunkSize%maxShardSize != 0 {
		return errors.New("maxShardSize should be pow of 2")
	}

	memory.mu.Lock()
	for idx, entry := range memory.memoryRegionList {
		if entry.addr == addr && entry.length == length {
			memory.memoryRegionList[idx].refCount++
			memory.mu.Unlock()
			return nil
		}
	}
	memory.memoryRegionList = append(memory.memoryRegionList,
		memoryRegion{addr: addr, length: length, refCount: 1})
	memory.mu.Unlock()

	for offset := uint64(0); offset < length; offset += maxChunkSize {
		chunkSize := maxChunkSize
		if chunkSize > length-offset {
			chunkSize = length - offset
		}
		wg.Add(1)
		go func(baseAddr uintptr) {
			defer wg.Done()
			err := memory.transferEngine.registerLocalMemory(baseAddr, chunkSize, "cpu:0")
			if err != nil {
				asyncError = err
			}
		}(addr + uintptr(offset))
	}
	wg.Wait()
	return asyncError
}

func (memory *RegisteredMemory) Remove(addr uintptr, length uint64, maxShardSize uint64) error {
	var wg sync.WaitGroup
	var asyncError error
	if maxChunkSize%maxShardSize != 0 {
		return errors.New("maxShardSize should be pow of 2")
	}

	memory.mu.Lock()
	for idx, entry := range memory.memoryRegionList {
		if entry.addr == addr && entry.length == length {
			entry := &memory.memoryRegionList[idx]
			entry.refCount--
			if entry.refCount == 0 {
				memory.memoryRegionList = append(memory.memoryRegionList[:idx],
					memory.memoryRegionList[idx+1:]...)
				break
			} else {
				memory.mu.Unlock()
				return nil
			}
		}
	}
	memory.mu.Unlock()

	for offset := uint64(0); offset < length; offset += maxShardSize {
		wg.Add(1)
		go func(baseAddr uintptr) {
			defer wg.Done()
			err := memory.transferEngine.unregisterLocalMemory(addr + uintptr(offset))
			if err != nil {
				asyncError = err
			}
		}(addr + uintptr(offset))
	}
	wg.Wait()
	return asyncError
}
