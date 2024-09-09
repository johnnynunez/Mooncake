#include "cuda.h"
#include "cuda_runtime.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <sys/time.h>
#include <cufile.h>

#include "transfer_engine_c.h"

#define WRITE 1
#define READ 0

const int BLOCK_SIZE = 4096;
const int ITERS = 16;
const char* META_SERVER = "192.168.3.72:2379";


int main(void)
{
    char *server_name = "optane14";
    // CUfileBatchHandle_t handle;
    // CUfileError_t e = cuFileBatchIOSetUp(handle, 8);
    // if (e.err != CU_FILE_SUCCESS) {
    //     printf("cuFileBatchIOSetUp failed with %d\n", e.err);
    //     return -1;
    // }
    transfer_engine_t *engine = createTransferEngine(META_SERVER);
    void **args = (void **)malloc(sizeof(void *));
    // args[0] = malloc(16);
    args[0] = "/mnt/nvme0n1/dsf/mooncake.img";
    // strcpy(args[0], "matrix");
    initTransferEngine(engine, server_name, server_name, 12345);

    transport_t nvmeof_xport = installOrGetTransport(engine, "nvmeof", args);
    
    segment_handle_t nvmeof_seg = openSegment(engine, "/mooncake/nvmeof/optane14");

    const size_t batch_size = 8;
    int length = batch_size * BLOCK_SIZE;
    void *buf;
    cudaMalloc(&buf, length);
    cudaMemset(buf, 0xab, length);

    registerLocalMemory(engine, buf, length, "", 0);

    struct timeval start_tv, stop_tv;

    gettimeofday(&start_tv, NULL);

    for (int i = 0; i < ITERS; ++i) {
        struct transfer_request nvmeof_transfers[batch_size];
        batch_id_t nvmeof_batch = allocateBatchID(nvmeof_xport, batch_size);

        for (int i = 0; i < batch_size; i++)
        {
            nvmeof_transfers[i] = (struct transfer_request){
                .opcode = READ,
                .source = buf + i * BLOCK_SIZE,
                .target_id = nvmeof_seg,
                .target_offset = i * BLOCK_SIZE,
                .length = BLOCK_SIZE,
            };
        }

        submitTransfer(nvmeof_xport, nvmeof_batch, nvmeof_transfers, batch_size);

        for (int i = 0; i < batch_size; i++)
        {
            // int ret;
            // getTransferStatus(xport, batch, i, &status);
            while (1) {
                struct transfer_status status;
                getTransferStatus(nvmeof_xport, nvmeof_batch, i, &status);
                // printf("task %d s %d", i, status.status);
                // LOG(INFO) << i <<  " status " << status.status;
                if (status.status == STATUS_FAILED || status.status == STATUS_COMPLETED) {
                    // printf("transfer %d: %zu bytes transferred, status = %d\n", i, status.transferred_bytes, status.status);
                    break;
                }
            }
        }
        freeBatchID(nvmeof_xport, nvmeof_batch);
    }


    gettimeofday(&stop_tv, NULL);

    double duration = (stop_tv.tv_sec - start_tv.tv_sec) + (stop_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    double throughput = ((double)ITERS * batch_size * BLOCK_SIZE) / duration / 1024 / 1024 / 1024;

    printf("throughput %.2lf GB/s duration %.2lf s\n", throughput, duration);
    unregisterLocalMemory(engine, buf);

    closeSegment(nvmeof_xport, nvmeof_seg);
    printf("before free batch\n");
    printf("after free batch\n");
    uninstallTransport(engine, "nvmeof");
    printf("after uninstall\n");
    destroyTransferEngine(engine);
    printf("after destroy\n");
    return 0;
}