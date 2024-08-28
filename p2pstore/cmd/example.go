package main

import (
	"context"
	"fmt"
	"os"
	"p2pstore"
	"syscall"
	"time"
	"unsafe"
)

const memoryMappedSize int = 1024 * 1024 * 1024

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

func doTrainer(ctx context.Context, store *p2pstore.P2PStore, name string) {
	addr, err := syscall.Mmap(-1, 0, memoryMappedSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	startTimestamp := time.Now()
	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(memoryMappedSize)}
	err = store.Register(ctx, name, addrList, sizeList, 64*1024*1024, "cpu:0")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Register failed: %v\n", err)
		os.Exit(1)
	}

	phaseOneTimestamp := time.Now()
	fmt.Println("Phase 1 duration ", phaseOneTimestamp.Sub(startTimestamp).Milliseconds())

	checkpointInfoList, err := store.List(ctx, "foo")
	if err != nil {
		fmt.Fprintf(os.Stderr, "List failed: %v\n", err)
		os.Exit(1)
	}

	fmt.Println(checkpointInfoList)
	fmt.Println("========================= IDLE ========================= ")
	time.Sleep(10 * time.Second)
	fmt.Println("========================= IDLE ========================= ")

	err = store.Unregister(ctx, name)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Unregister failed: %v\n", err)
		os.Exit(1)
	}

	if err := syscall.Munmap(addr); err != nil {
		fmt.Fprintf(os.Stderr, "Munmap failed: %v\n", err)
		os.Exit(1)
	}
}

func trainer() {
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Second)
	defer cancel()

	hostname, err := os.Hostname()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting hostname: %v\n", err)
		os.Exit(1)
	}

	nicPriorityMatrix := "{ \"cpu:0\": [[\"mlx5_2\"], []]}"
	store, err := p2pstore.NewP2PStore("http://optane21:2379", hostname, nicPriorityMatrix)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating checkpoint engine: %v\n", err)
		os.Exit(1)
	}

	doTrainer(ctx, store, "foo/bar")
	doTrainer(ctx, store, "foo/bar2")

	err = store.Close()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Shutdown failed: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("ALL DONE")
}

func doInferencer(ctx context.Context, store *p2pstore.P2PStore, name string) {
	addr, err := syscall.Mmap(-1, 0, memoryMappedSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	startTimestamp := time.Now()
	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(memoryMappedSize)}
	err = store.GetReplica(ctx, name, addrList, sizeList)
	if err != nil {
		fmt.Fprintf(os.Stderr, "GetLocalCheckpoint failed: %v\n", err)
		os.Exit(1)
	}

	phaseOneTimestamp := time.Now()
	fmt.Println("Phase 1 duration ", phaseOneTimestamp.Sub(startTimestamp).Milliseconds())
	// Cloned

	err = store.DeleteReplica(ctx, name)
	if err != nil {
		fmt.Fprintf(os.Stderr, "DeleteReplica failed: %v\n", err)
		os.Exit(1)
	}

	phaseTwoTimestamp := time.Now()
	fmt.Println("Phase 2 duration ", phaseTwoTimestamp.Sub(startTimestamp).Milliseconds())

	if err := syscall.Munmap(addr); err != nil {
		fmt.Fprintf(os.Stderr, "Munmap failed: %v\n", err)
		os.Exit(1)
	}

	phaseThreeTimestamp := time.Now()
	fmt.Println("Phase 3 duration ", phaseThreeTimestamp.Sub(startTimestamp).Milliseconds())

	fmt.Println("ALL DONE")
}

func inferencer() {
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Second)
	defer cancel()

	hostname, err := os.Hostname()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting hostname: %v\n", err)
		os.Exit(1)
	}

	nicPriorityMatrix := "{ \"cpu:0\": [[\"mlx5_2\"], []]}"
	store, err := p2pstore.NewP2PStore("http://optane21:2379", hostname, nicPriorityMatrix)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating checkpoint engine: %v\n", err)
		os.Exit(1)
	}

	doInferencer(ctx, store, "foo/bar")
	fmt.Println("========================= IDLE ========================= ")
	time.Sleep(10 * time.Second)
	fmt.Println("========================= IDLE ========================= ")
	doInferencer(ctx, store, "foo/bar2")

	err = store.Close()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Shutdown failed: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("ALL DONE")
}
