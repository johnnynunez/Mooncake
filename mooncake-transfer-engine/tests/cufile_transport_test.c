#include <bits/stdint-uintn.h>
#include <cufile.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cuda.h"
#include "cuda_runtime.h"
#include "transfer_engine_c.h"

#define WRITE 1
#define READ 0
#define GB(x) ((x)*1024 * 1024 * 1024)

const int ITERS = 10000;
int batch_size = 32;
int block_size = 4096;
int duration = 10;
const int NR_THREADS = 4;
const char *META_SERVER = "192.168.3.72:2379";

volatile bool running = true;
uint64_t batch_count = 0;

typedef struct {
    transport_t *xport;
    segment_handle_t seg;
    int thread_id;
    void *addr;
} worker_args_t;

void *worker(void *args_) {
    // printf("submit transfer %d\n", i);
    worker_args_t *args = (worker_args_t *)args_;
    transport_t xport = args->xport;
    segment_handle_t seg = args->seg;
    void *addr = args->addr;  // Per Thread Buffer, should be registered
    uint64_t local_batch_count = 0;
    while (running) {
        struct transfer_request nvmeof_transfers[batch_size];
        batch_id_t nvmeof_batch = allocateBatchID(xport, batch_size);
        for (int i = 0; i < batch_size; i++) {
            nvmeof_transfers[i] = (struct transfer_request){
                .opcode = READ,
                .source = addr + i * block_size,
                .target_id = seg,
                .target_offset = i * block_size,
                .length = block_size,
            };
        }
        submitTransfer(xport, nvmeof_batch, nvmeof_transfers, batch_size);
        for (int i = 0; i < batch_size; i++) {
            while (1) {
                struct transfer_status status;
                getTransferStatus(xport, nvmeof_batch, i, &status);
                // printf("task %d s %d\n", -1, status.status);
                // LOG(INFO) << i <<  " status " << status.status;
                if (status.status == STATUS_FAILED) {
                    printf("transfer %d: bytes transferred, status = failed!",
                           i);
                    break;
                }
                if (status.status == STATUS_FAILED ||
                    status.status == STATUS_COMPLETED) {
                    // printf("transfer %d: %zu bytes transferred, status =
                    // %d\n", i, status.transferred_bytes, status.status);
                    break;
                }
            }
        }
        freeBatchID(xport, nvmeof_batch);
        ++local_batch_count;
    }
    __sync_fetch_and_add(&batch_count, local_batch_count);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s <file_name> <gpu_id> <block_size(KB)>\n", argv[0]);
        return -1;
    }
    char *server_name = "localhost";
    int gpu_id = atoi(argv[2]);
    block_size = atoi(argv[3]) * 1024;
    cudaSetDevice(gpu_id);
    printf("read file %s on gpu %d\n", argv[1], gpu_id);
    transfer_engine_t *engine = createTransferEngine(META_SERVER);
    void **args = (void **)malloc(sizeof(void *));
    args[0] = argv[1];
    initTransferEngine(engine, server_name, server_name, 12345);

    transport_t nvmeof_xport = installOrGetTransport(engine, "nvmeof", args);

    segment_handle_t nvmeof_seg =
        openSegment(engine, "/mooncake/nvmeof/localhost");
    long long length = GB(1);
    void *start[NR_THREADS];
    for (int i = 0; i < NR_THREADS; ++i) {
        void *buf;
        cudaMalloc(&buf, length);
        cudaMemset(buf, 0xab, length);
        registerLocalMemory(engine, buf, length, "", 0);
        start[i] = buf;
    }

    struct timeval start_tv, stop_tv;

    gettimeofday(&start_tv, NULL);

    pthread_t threads[NR_THREADS];
    for (int i = 0; i < NR_THREADS; i++) {
        worker_args_t *args = (worker_args_t *)malloc(sizeof(worker_args_t));
        args->xport = nvmeof_xport;
        args->seg = nvmeof_seg;
        args->thread_id = i;
        args->addr = start[i];
        pthread_create(&threads[i], NULL, worker, args);
    }

    sleep(duration);
    running = false;

    for (int i = 0; i < NR_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&stop_tv, NULL);

    double duration = (stop_tv.tv_sec - start_tv.tv_sec) +
                      (stop_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    double throughput = ((double)batch_count * batch_size * block_size) /
                        duration / 1024 / 1024 / 1024;

    printf("throughput %.2lf GB/s duration %.2lf s\n", throughput, duration);
    for (int i = 0; i < NR_THREADS; ++i) {
        unregisterLocalMemory(engine, start[i]);
        cudaFree(start[i]);
    }

    closeSegment(nvmeof_xport, nvmeof_seg);
    printf("before free batch\n");
    printf("after free batch\n");
    uninstallTransport(engine, "nvmeof");
    printf("after uninstall\n");
    destroyTransferEngine(engine);
    printf("after destroy\n");
    return 0;
}