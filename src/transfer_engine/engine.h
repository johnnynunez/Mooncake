// User-side API.

#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TransferEngine transfer_engine;
typedef struct Transport transport;

enum transfer_opcode
{
    READ,
    WRITE
};

struct transfer_request
{
    enum transfer_opcode opcode;
    void *buffer;
    struct transfer_segment *segment;
    uint64_t offset;
    size_t length;

    /// @brief internal fields; should not be touched by user. Can be accessed by transport implementors.
    struct
    {
        // set by engine to indicate which batch this request belongs to.
        struct transfer_batch *batch;
        // can be used by transport implemetors to store request contexts.
        union
        {
            void *context;
            uint64_t tag;
        };
    } __internal;
};

enum transfer_status_code
{
    WAITING = 0,
    PENDING,
    INVALID,
    CANNELED,
    COMPLETED,
    TIMEOUT,
    FAILED
};

struct transfer_status
{
    enum transfer_status_code code;
    size_t transferred_bytes;
};

struct transfer_segment
{
    /// @brief transport instance
    transport *xport;
    /// @brief transport-specific context
    void *context;
};

struct transfer_batch
{
    /// @brief transfer batch, should be a Transport*
    transport *xport;
    /// @brief maximum number of outstanding work requests
    unsigned max_nr;
    /// @brief transport-specific context
    void *context;
};

#ifdef __cplusplus
extern "C"
{
#endif

    /// By default, all APIs return 0 (or nonnull pointer) on success, negative number (or NULL pointer) on error.
    /// The errno is set accordingly if an error occured.

    transfer_engine *init_transfer_engine();

    int free_transfer_engine(transfer_engine *engine);

    transport *install_transport(transfer_engine *engine, const char *proto, const char *path_prefix,
                                        void **args);

    int uninstall_transport(transfer_engine *engine, transport *xport);

    struct transfer_segment *open_segment(transfer_engine *engine, const char *name);

    int close_segment(struct transfer_segment *segment);

    struct transfer_batch *alloc_transfer_batch(transport *xport, unsigned max_nr);

    int free_transfer_batch(struct transfer_batch *batch);

    /// @return The number of successfully submitted transfers on success. If that number is less than nr, errno is set.
    int submit_transfers(struct transfer_batch *batch, unsigned nr, struct transfer_request *transfers);

    /// @brief Get the status of a submitted transfer. This function shall not be called again after completion.
    /// Forget to call this function might cause resource leak.
    /// @return Return 1 on completed (either success or failure); 0 if still in progress.
    int get_transfer_status(struct transfer_request *transfer, struct transfer_status *status);

#ifdef __cplusplus
}
#endif
