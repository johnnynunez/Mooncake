package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"p2pstore"
	"syscall"
	"time"
	"unsafe"
)

var (
	command               string
	metadataServer        string
	localServerName       string
	deviceName            string
	nicPriorityMatrixPath string
	fileSize              int
)

func main() {
	flag.StringVar(&metadataServer, "metadata_server", "localhost:2379", "Metadata server address")
	flag.StringVar(&localServerName, "local_server_name", "", "Local server name")
	flag.StringVar(&deviceName, "device_name", "mlx5_2", "RNIC device name")
	flag.StringVar(&nicPriorityMatrixPath, "nic_priority_matrix", "", "Path to NIC priority matrix file (Advanced)")
	flag.IntVar(&fileSize, "file_size_mb", 40960, "File size in MB")
	flag.Parse()

	fileSize = fileSize * 1024 * 1024
	if len(localServerName) == 0 {
		var err error
		localServerName, err = os.Hostname()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error getting hostname: %v\n", err)
			os.Exit(1)
		}
	}

	args := flag.Args()
	if len(args) != 2 {
		fmt.Println(`
Usage: ./p2p-store-example <trainer|inferencer> 
		                   [--metadata_server=localhost:2379] 
		                   [--local_server_name=localhost:12345] 
						   [--device_name=mlx5_2] 
						   [--file_size_mb=40960]
		`)
		os.Exit(1)
	}

	command = args[0]
	switch command {
	case "trainer":
		trainer()
	case "inferencer":
		inferencer()
	default:
		fmt.Printf("Invalid command: %s\n", command)
		os.Exit(1)
	}
}

func doTrainer(ctx context.Context, store *p2pstore.P2PStore, name string) {
	addr, err := syscall.Mmap(-1, 0, fileSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	startTimestamp := time.Now()
	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(fileSize)}
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
	time.Sleep(20 * time.Second)
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

	store, err := p2pstore.NewP2PStore(metadataServer, localServerName, getPriorityMatrix())
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

func getPriorityMatrix() string {
	if len(nicPriorityMatrixPath) != 0 {
		data, err := ioutil.ReadFile(nicPriorityMatrixPath)
		if err != nil {
			fmt.Println("Error reading file:", err)
			os.Exit(1)
		}
		return string(data)
	} else {
		return "{ \"cpu:0\": [[\"" + deviceName + "\"], []]}"
	}
}

func doInferencer(ctx context.Context, store *p2pstore.P2PStore, name string) {
	addr, err := syscall.Mmap(-1, 0, fileSize, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_ANON|syscall.MAP_PRIVATE)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Mmap failed: %v\n", err)
		os.Exit(1)
	}

	startTimestamp := time.Now()
	addrList := []uintptr{uintptr(unsafe.Pointer(&addr[0]))}
	sizeList := []uint64{uint64(fileSize)}
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

	store, err := p2pstore.NewP2PStore(metadataServer, localServerName, getPriorityMatrix())
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating checkpoint engine: %v\n", err)
		os.Exit(1)
	}

	doInferencer(ctx, store, "foo/bar")
	fmt.Println("========================= IDLE ========================= ")
	time.Sleep(20 * time.Second)
	fmt.Println("========================= IDLE ========================= ")
	doInferencer(ctx, store, "foo/bar2")

	err = store.Close()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Shutdown failed: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("ALL DONE")
}
