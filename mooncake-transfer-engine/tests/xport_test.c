#include "cuda.h"
#include "cuda_runtime.h"
#include "transfer_engine_c.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE 1
#define READ 0

int main(void)
{
    char *metadata = "etcd_server:2379";
    char *server_name = "optane10";
    transfer_engine_t *engine = createTransferEngine(metadata);
    void **args = (void **)malloc(sizeof(void *));
    // args[0] = malloc(16);
    args[0] = "/mnt/nvme0n1/dsf/gds.txt";
    // strcpy(args[0], "matrix");
    initTransferEngine(engine, server_name, server_name, 12345);

    transport_t rdma_xport = installOrGetTransport(engine, "rdma", args);
    transport_t nvmeof_xport = installOrGetTransport(engine, "nvmeof", args);

    segment_handle_t rdma_seg = openSegment(engine, "ram/optane11");
    segment_handle_t nvmeof_seg = openSegment(engine, "nvmeof/optane11");

    const size_t batch_size = 8;
    batch_id_t rdma_batch = allocateBatchID(rdma_xport, batch_size);
    batch_id_t nvmeof_batch = allocateBatchID(nvmeof_xport, batch_size);
    int length = batch_size * 1024 * 2;
    void *buf;
    cudaMalloc(&buf, length);
    cudaMemset(buf, 0xab, length);

    // memset(buf, 1, 1024 * batch_size);
    registerLocalMemory(engine, buf, length, "", 0);

    struct transfer_request rdma_transfers[batch_size], nvmeof_transfers[batch_size];
    for (size_t i = 0; i < batch_size; i++)
    {
        rdma_transfers[i] = (struct transfer_request){
            .opcode = WRITE,
            .source = buf + i * 1024,
            .target_id = rdma_seg,
            .target_offset = i * 1024,
            .length = 1024,
        };
        nvmeof_transfers[i] = (struct transfer_request){
            .opcode = READ,
            .source = buf + i * 1024,
            .target_id = LOCAL_SEGMENT,
            .target_offset = i * 1024,
            .length = 1024,
        };
    }
    submitTransfer(rdma_xport, rdma_batch, rdma_transfers, batch_size);
    submitTransfer(nvmeof_xport, nvmeof_batch, nvmeof_transfers, batch_size);

    for (size_t i = 0; i < batch_size; i++)
    {
        struct transfer_status status;
        int ret;
        // getTransferStatus(xport, batch, i, &status);
        while ((ret = getTransferStatus(nvmeof_xport, nvmeof_batch, i, &status)) == 0)
        {
            // busy waiting for nvme
        };
        while ((ret = getTransferStatus(rdma_xport, rdma_batch, i, &status)) == 0)
        {
            // busy waiting for rdma
        };
        printf("transfer %ld: %zu bytes transferred, status = %d\n", i, status.transferred_bytes, status.status);
    }

    unregisterLocalMemory(engine, buf);

    closeSegment(rdma_xport, rdma_seg);
    closeSegment(nvmeof_xport, nvmeof_seg);

    freeBatchID(rdma_xport, rdma_batch);
    freeBatchID(nvmeof_xport, nvmeof_batch);
    uninstallTransport(engine, "rdma");
    uninstallTransport(engine, "nvmeof");
    destroyTransferEngine(engine);
    return 0;
}