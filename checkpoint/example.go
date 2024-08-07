package main

import (
	"fmt"
	"os"
	"syscall"
	"time"
	"unsafe"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Printf("Usage: %s [trainer|inferencer]\n", os.Args[0])
		os.Exit(0)
	}

	command := os.Args[1]
	switch command {
	case "trainer":
		trainer()
	case "inferencer":
		inferencer()
	default:
		fmt.Printf("Unknown command: %s\n", command)
	}

}

func trainer() {
	hostname, err := os.Hostname()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting hostname: %v\n", err)
		os.Exit(1)
	}

	nicPriorityMatrix := "{ \"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]]}"
	checkpointEngine, err := NewCheckpointEngine("http://test-8:2379", hostname, nicPriorityMatrix)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating checkpoint engine: %v\n", err)
		os.Exit(1)
	}

	const memoryMappedSize int = 1024 * 1024 * 1024
	addr, err := syscall.Mmap(-1, 0, memoryMappedSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(memoryMappedSize)}
	err = checkpointEngine.RegisterCheckpoint("foo/bar", addrList, sizeList, 64*1024*1024)
	if err != nil {
		fmt.Fprintf(os.Stderr, "UnregisterCheckpoint failed: %v\n", err)
		os.Exit(1)
	}

	checkpointInfoList, err := checkpointEngine.GetCheckpointInfo("foo")
	if err != nil {
		fmt.Fprintf(os.Stderr, "GetCheckpointInfo failed: %v\n", err)
		os.Exit(1)
	}

	fmt.Println(checkpointInfoList)
	fmt.Println("========================= IDLE ========================= ")
	time.Sleep(100 * time.Second)
	fmt.Println("========================= IDLE ========================= ")

	err = checkpointEngine.UnregisterCheckpoint("foo/bar")
	if err != nil {
		fmt.Fprintf(os.Stderr, "UnregisterCheckpoint failed: %v\n", err)
		os.Exit(1)
	}

	if err := syscall.Munmap(addr); err != nil {
		fmt.Fprintf(os.Stderr, "Munmap failed: %v\n", err)
		os.Exit(1)
	}

	err = checkpointEngine.Close()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Shutdown failed: %v\n", err)
		os.Exit(1)
	}
}

func inferencer() {
	hostname, err := os.Hostname()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting hostname: %v\n", err)
		os.Exit(1)
	}

	nicPriorityMatrix := "{ \"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]]}"
	checkpointEngine, err := NewCheckpointEngine("http://test-8:2379", hostname, nicPriorityMatrix)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating checkpoint engine: %v\n", err)
		os.Exit(1)
	}

	const memoryMappedSize int = 1024 * 1024 * 1024
	addr, err := syscall.Mmap(-1, 0, memoryMappedSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(memoryMappedSize)}
	err = checkpointEngine.GetLocalCheckpoint("foo/bar", addrList, sizeList)
	if err != nil {
		fmt.Fprintf(os.Stderr, "UnregisterCheckpoint failed: %v\n", err)
		os.Exit(1)
	}

	// Cloned

	err = checkpointEngine.DeleteLocalCheckpoint("foo/bar")
	if err != nil {
		fmt.Fprintf(os.Stderr, "UnregisterCheckpoint failed: %v\n", err)
		os.Exit(1)
	}

	if err := syscall.Munmap(addr); err != nil {
		fmt.Fprintf(os.Stderr, "Munmap failed: %v\n", err)
		os.Exit(1)
	}
}
