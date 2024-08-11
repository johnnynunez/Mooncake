// For transport implementers.

#pragma once

#include <cstddef>
#include <errno.h>
#include <iostream>
#include <stddef.h>
#include <stdint.h>

#include "transfer_metadata.h"

namespace mooncake
{
    class TransferMetadata;
    /// By default, these functions return 0 (or non-null pointer) on success and return -1 (or null pointer) on failure.
    /// The errno is set accordingly on failure.
    struct Transport
    {
        friend struct MultiTransferEngine;

        using SegmentID = int32_t;
        const static SegmentID LOCAL_SEGMENT_ID = 0;

        using BatchID = uint64_t;
        const static BatchID INVALID_BATCH_ID = UINT64_MAX;

        struct TransferRequest
        {
            enum OpCode
            {
                READ,
                WRITE
            };

            OpCode opcode;
            void *source;
            SegmentID target_id;
            uint64_t target_offset;
            size_t length;
        };

        enum TransferStatusEnum
        {
            WAITING,
            PENDING,
            INVALID,
            CANNELED,
            COMPLETED,
            TIMEOUT,
            FAILED
        };

        struct TransferStatus
        {
            TransferStatusEnum s;
            size_t transferred_bytes;
        };

        using BufferDesc = TransferMetadata::BufferDesc;
        using SegmentDesc = TransferMetadata::SegmentDesc;
        using HandShakeDesc = TransferMetadata::HandShakeDesc;

    public:
        /// @brief install the transport with specified args. The args parameter is interpreted by the
        /// transport implementation.
        virtual int install(void **args) = 0;

        /// @brief Register a memory region for use with this transport.
        virtual int registerLocalMemory(void *addr, size_t size, const std::string &location) = 0;

        /// @brief Unegister a memory region for use with this transport.
        virtual int unregisterLocalMemory(void *addr) = 0;

        /// @brief Open the segment with specified path.
        virtual SegmentID openSegment(const std::string &path) = 0;

        /// @brief Close the segment.
        virtual int closeSegment(SegmentID segment_id) = 0;

        /// @brief Create a batch with specified maximum outstanding transfers.
        virtual BatchID allocateBatchID(size_t batch_size) = 0;

        /// @brief Free an allocated batch.
        virtual int freeBatchID(BatchID batch_id) = 0;

        /// @brief Submit a batch of transfer requests to the batch.
        /// @return The number of successfully submitted transfers on success. If that number is less than nr, errno is set.
        virtual int submitTransfer(BatchID batch_id,
                                   const std::vector<TransferRequest> &entries) = 0;

        /// @brief Get the status of a submitted transfer. This function shall not be called again after completion.
        /// @return Return 1 on completed (either success or failure); 0 if still in progress.
        virtual int getTransferStatus(BatchID batch_id, size_t task_id,
                                      TransferStatus &status) = 0;

    private:
        virtual const char *getName() = 0;
    };

    class DummyTransport : public Transport
    {
    public:
        int install(void **args) override
        {
            // 1. get
            char* arg1 = (char*)args[0];
            char* arg2 = (char*)args[1];
            std::cout << "install, arg1: " << arg1 << ", arg2: " << arg2 << std::endl;
            return 0;
        }

        int registerLocalMemory(void *addr, size_t size, const std::string &location) override
        {
            std::cout << "registerLocalMemory, addr: " << addr << ", size: " << size << ", location: " << location << std::endl;
            return 0;
        }

        int unregisterLocalMemory(void *addr) override
        {   
            std::cout << "unregisterLocalMemory, addr: " << addr << std::endl;
            return 0;
        }

        SegmentID openSegment(const std::string &path) override
        {
            std::cout << "openSegment, path: " << path << std::endl;
            return 1;
        }

        int closeSegment(SegmentID segment_id) override
        {
            std::cout << "closeSegment, segment_id: " << segment_id << std::endl;
            return 0;
        }

        BatchID allocateBatchID(size_t batch_size) override
        {
            std::cout << "allocateBatchID, batch_size: " << batch_size << std::endl;
            return 0x7fffffffffffffff;
        }

        int freeBatchID(BatchID batch_id) override
        {
            std::cout << "freeBatchID, batch_id: " << batch_id << std::endl;
            return 0;
        }

        int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) override
        {
            std::cout << "submitTransfer, batch_id: " << batch_id << ", entries.size: " << entries.size() << std::endl;
            return entries.size();
        }

        /// @brief Get the status of a submitted transfer. This function shall not be called again after completion.
        /// @return Return 1 on completed (either success or failure); 0 if still in progress.
        int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) override
        {
            std::cout << "getTransferStatus, batch_id: " << batch_id << ", task_id: " << task_id << std::endl;
            status.s = COMPLETED;
            status.transferred_bytes = 100;
            return 0;
        }

    private:
        const char *getName() override
        {
            return "dummy";
        }
    };
}