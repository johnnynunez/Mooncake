// For transport implementers.

#pragma once

#include <cstddef>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "engine.h"

/// By default, these functions return 0 (or non-null pointer) on success and return -1 (or null pointer) on failure.
/// The errno is set accordingly on failure.
struct Transport {
    friend struct TransferEngine;
public:
    /// @brief install the transport with specified args. The args parameter is interpreted by the
    /// transport implementation.
    virtual int install(void **args) = 0;

    /// @brief Register a memory region for use with this transport.
    virtual int registerMemory(void *addr, size_t size) = 0;

    /// @brief Unegister a memory region for use with this transport.
    virtual int unregisterMemory(void *addr) = 0;

    /// @brief Open the segment with specified path.
    virtual int openSegment(const char *path, struct transfer_segment *segment) = 0;

    /// @brief Close the segment.
    virtual int closeSegment(struct transfer_segment *segment) = 0;

    /// @brief Create a batch with specified maximum outstanding transfers.
    virtual int allocBatch(struct transfer_batch *batch, unsigned max_nr) = 0;

    /// @brief Free an allocated batch.
    virtual int freeBatch(struct transfer_batch *batch) = 0;

    /// @brief Submit a batch of transfer requests to the batch.
    /// @return The number of successfully submitted transfers on success. If that number is less than nr, errno is set.
    virtual int submitTransfers(struct transfer_batch *batch, unsigned nr, struct transfer_request *transfers) = 0;

    /// @brief Get the status of a submitted transfer. This function shall not be called again after completion.
    /// @return Return 1 on completed (either success or failure); 0 if still in progress.
    virtual int getTransferStatus(struct transfer_request *transfer, struct transfer_status *status) = 0;

private:
    virtual const char* getName() = 0;

};

class DummyTransport : public Transport {
public:
    int install(void **args) override {
        return 0;
    }

    int registerMemory(void *addr, size_t size) override {
        return 0;
    }

    int unregisterMemory(void *addr) override {
        return 0;
    }

    int openSegment(const char *path, struct transfer_segment *segment) override {
        return 0;
    }

    int closeSegment(struct transfer_segment *segment) override {
        return 0;
    }

    int allocBatch(struct transfer_batch *batch, unsigned max_nr) override {
        return 0;
    }

    int freeBatch(struct transfer_batch *batch) override {
        return 0;
    }

    int submitTransfers(struct transfer_batch *batch, unsigned nr, struct transfer_request *transfers) override {
        return 0;
    }

    int getTransferStatus(struct transfer_request *transfer, struct transfer_status *status) override {
        return 0;
    }
    
private:
    const char* getName() override {
        return "dummy";
    }
};