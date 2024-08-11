// transfer_engine_c.h
// Copyright (C) 2024 Feng Ren

#ifndef TRANSFER_ENGINE_C
#define TRANSFER_ENGINE_C

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define segment_id_t int32_t
#define batch_id_t uint64_t
#define LOCAL_SEGMENT (0)
#define INVALID_BATCH UINT64_MAX

#define OPCODE_READ (0)
#define OPCODE_WRITE (1)

    struct transfer_request
    {
        int opcode;
        void *source;
        segment_id_t target_id;
        uint64_t target_offset;
        uint64_t length;
    };

    typedef struct transfer_request transfer_request_t;

#define STATUS_WAITING (0)
#define STATUS_PENDING (1)
#define STATUS_INVALID (2)
#define STATUS_CANNELED (3)
#define STATUS_COMPLETED (4)
#define STATUS_TIMEOUT (5)
#define STATUS_FAILED (6)

    struct transfer_status
    {
        int status;
        uint64_t transferred_bytes;
    };

    typedef struct transfer_status transfer_status_t;

    struct buffer_entry
    {
        void *addr;
        size_t length;
    };
    typedef struct buffer_entry buffer_entry_t;

    typedef void *transfer_engine_t;
    typedef void *transport_t;

    transfer_engine_t createTransferEngine();

    transport_t installTransport(transfer_engine_t engine,
                                 const char *proto,
                                 const char *path_prefix,
                                 void** args);

    int uninstallTransport(transfer_engine_t engine, transport_t xport);

    segment_id_t openSegment(transport_t xport, const char *segment_name);
    // segment_id_t getSegmentID(transfer_engine_t engine, const char *segment_name);
    int closeSegment(transport_t xport, segment_id_t seg_id);

    void destroyTransferEngine(transfer_engine_t engine);

    int registerLocalMemory(transport_t xport, void *addr, size_t length, const char *location);

    int unregisterLocalMemory(transport_t xport, void *addr);

    int registerLocalMemoryBatch(transfer_engine_t engine, buffer_entry_t *buffer_list, size_t buffer_len, const char *location);

    int unregisterLocalMemoryBatch(transfer_engine_t engine, void **addr_list, size_t addr_len);

    batch_id_t allocateBatchID(transport_t xport, size_t batch_size);

    int submitTransfer(transport_t xport,
                       batch_id_t batch_id,
                       struct transfer_request *entries,
                       size_t count);

    int getTransferStatus(transport_t xport,
                          batch_id_t batch_id,
                          size_t task_id,
                          struct transfer_status *status);

    int freeBatchID(transport_t xport, batch_id_t batch_id);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // TRANSFER_ENGINE_C