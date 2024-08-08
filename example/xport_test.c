#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transfer_engine/engine.h"

void error_and_die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(void)
{
    transfer_engine *engine = init_transfer_engine();
    if (!engine)
    {
        error_and_die("init_transfer_engine");
    }
    transport *xport = install_transport(engine, "dummy", "file", NULL);
    if (!xport)
    {
        error_and_die("install_transport");
    }
    struct transfer_segment *seg = open_segment(engine, "file:/dev/zero");
    if (!seg)
    {
        error_and_die("open_segment");
    }
    struct transfer_segment *seg_stdout = open_segment(engine, "file:/dev/stdout");
    if (!seg_stdout)
    {
        error_and_die("open_segment");
    }
    const int batch_size = 8;
    struct transfer_batch *batch = alloc_transfer_batch(xport, batch_size);
    if (!batch)
    {
        error_and_die("alloc_transfer_batch");
    }

    char *buf = (char*)malloc(1024 * batch_size);
    memset(buf, 1, 1024 * batch_size);

    struct transfer_request transfers[batch_size + 1];
    for (int i = 0; i < batch_size; i++)
    {
        transfers[i] = (struct transfer_request){
            .opcode = READ,
            .buffer = buf + i * 1024,
            .segment = seg,
            .offset = 0,
            .length = 1024,
        };
    }
    {
        char *buf = "hello, world!\n";
        transfers[batch_size] = (struct transfer_request){
            .opcode = WRITE,
            .buffer = buf,
            .segment = seg_stdout,
            .offset = 0,
            .length = strlen(buf),

        };
    }
    if (submit_transfers(batch, batch_size + 1, transfers) != batch_size + 1)
    {
        error_and_die("submit_transfers");
    }

    for (int i = 0; i < batch_size + 1; i++)
    {
        struct transfer_status status;
        int ret;
        // while ((ret = get_transfer_status(&transfers[i], &status)) == 0)
        // {
        //     // busy waiting
        // };
        if (ret < 0)
        {
            error_and_die("get_transfer_status");
        }
        printf("transfer %d: %zu bytes transferred, status = %d\n", i, status.transferred_bytes, status.code);
    }

    // if (buf[0] == 0 && memcmp(buf, buf + 1, 1024 * batch_size - 1) == 0)
    // {
    //     printf("OK: buf is zeroed\n");
    // }
    // else
    // {
    //     printf("ERROR: buf is not zeroed\n");
    // }

    free_transfer_batch(batch);
    uninstall_transport(engine, xport);
    free_transfer_engine(engine);
}
