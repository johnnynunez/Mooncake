package checkpoint

import (
	"sort"
	"sync"
)

type MemoryRegionEntry struct {
	Addr     uintptr
	Length   uint64
	RefCount int
}

type RegisteredMemory struct {
	entries        []MemoryRegionEntry
	transferEngine *TransferEngine
	mu             sync.Mutex
}

func NewRegisteredMemory(transferEngine *TransferEngine) *RegisteredMemory {
	return &RegisteredMemory{transferEngine: transferEngine}
}

func (memory *RegisteredMemory) Add(addr uintptr, length uint64) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()

	var newEntries []MemoryRegionEntry

	// assert: current.Addr + current.length == addr + length
	current := MemoryRegionEntry{
		Addr:     addr,
		Length:   length,
		RefCount: 1,
	}

	currentEndAddr := addr + uintptr(length)
	for _, existing := range memory.entries {
		existingEndAddr := existing.Addr + uintptr(existing.Length)

		if currentEndAddr <= existing.Addr {
			break
		}

		if current.Addr >= existingEndAddr {
			continue
		}

		// Overlapped
		if current.Addr < existing.Addr {
			newEntries = append(newEntries, MemoryRegionEntry{
				Addr:     current.Addr,
				Length:   uint64(existing.Addr - current.Addr),
				RefCount: 1,
			})
		}

		if currentEndAddr > existingEndAddr {
			current.Addr = existingEndAddr
			current.Length = uint64(currentEndAddr - current.Addr)
		} else {
			current.Length = 0
		}

		existing.RefCount++
	}

	if current.Length != 0 {
		newEntries = append(newEntries, current)
	}

	for _, currentRange := range newEntries {
		err := memory.transferEngine.registerLocalMemory(currentRange.Addr, currentRange.Length, "cpu:0")
		if err != nil {
			return err
		}

		insertIndex := sort.Search(len(memory.entries), func(i int) bool {
			return memory.entries[i].Addr >= currentRange.Addr
		})

		// Insert new range at the found index
		memory.entries = append(memory.entries[:insertIndex],
			append([]MemoryRegionEntry{currentRange}, memory.entries[insertIndex:]...)...)
	}

	return nil
}

func (memory *RegisteredMemory) Remove(addr uintptr, length uint64) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()

	var removeEntries []MemoryRegionEntry
	var nextEntries []MemoryRegionEntry

	currentAddr := addr
	currentEndAddr := addr + uintptr(length)

	for _, existing := range memory.entries {
		existingEndAddr := existing.Addr + uintptr(existing.Length)
		if existing.Addr < currentEndAddr && existing.Addr == currentAddr {
			existing.RefCount--
			if existing.RefCount == 0 {
				removeEntries = append(removeEntries, existing)
			} else {
				nextEntries = append(nextEntries, existing)
			}
			currentAddr = existingEndAddr
		} else {
			nextEntries = append(nextEntries, existing)
		}
	}

	memory.entries = nextEntries
	for _, currentRange := range removeEntries {
		err := memory.transferEngine.unregisterLocalMemory(currentRange.Addr)
		if err != nil {
			return err
		}
	}

	return nil
}
