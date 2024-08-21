#include "nvmeof_transport.h"
#include "transfer_engine/cufile_context.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transport.h"
#include <bits/stdint-uintn.h>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <utility>

namespace mooncake
{

    Transport::TransferStatusEnum from_cufile_transfer_status(CUfileStatus_t status)
    {
        switch (status)
        {
        case CUFILE_WAITING:
            return Transport::WAITING;
        case CUFILE_PENDING:
            return Transport::PENDING;
        case CUFILE_INVALID:
            return Transport::INVALID;
        case CUFILE_CANCELED:
            return Transport::CANNELED;
        case CUFILE_COMPLETE:
            return Transport::COMPLETED;
        case CUFILE_TIMEOUT:
            return Transport::TIMEOUT;
        case CUFILE_FAILED:
            return Transport::FAILED;
        default:
            return Transport::FAILED;
        }
    }

    BatchID NVMeoFTransport::allocateBatchID(size_t batch_size)
    {
        // TODO: this can be moved to upper layer?
        auto cufile_desc = new CuFileBatchDesc();
        auto batch_id = Transport::allocateBatchID(batch_size);
        auto &batch_desc = *((BatchDesc *)(batch_id));
        cufile_desc->cufile_io_params.reserve(batch_size);
        cufile_desc->cufile_events_buf.resize(batch_size);
        cufile_desc->transfer_status.reserve(batch_size);
        cufile_desc->nr_completed = 0;
        // TODO: 在按照文件拆分之后再设置 size
        batch_desc.context = cufile_desc;
        CUFILE_CHECK(cuFileBatchIOSetUp(&cufile_desc->handle, batch_size));
        return batch_id;
    }

    int NVMeoFTransport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &cufile_desc = *((CuFileBatchDesc *)(batch_desc.context));
        unsigned nr = cufile_desc.cufile_io_params.size();
        LOG(INFO) << "get t n " << nr;
        CUFILE_CHECK(cuFileBatchIOGetStatus(cufile_desc.handle, 0, &nr, cufile_desc.cufile_events_buf.data(), NULL));
        auto &event = cufile_desc.cufile_events_buf[task_id];
        unsigned idx = (intptr_t)event.cookie;
        TransferStatus transfer_status;
        transfer_status.s = from_cufile_transfer_status(event.status);
        if (transfer_status.s == COMPLETED)
        {
            transfer_status.transferred_bytes = event.ret;
            nr_completed += 1;
        }
        status = transfer_status;
        return 1;
    }

    int NVMeoFTransport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &cufile_desc = *((CuFileBatchDesc *)(batch_desc.context));
        if (cufile_desc.cufile_io_params.size() + entries.size() > batch_desc.batch_size)
            return -1;

        // std::unordered_map<std::shared_ptr<RdmaContext>, std::vector<Slice *>> slices_to_post;
        size_t start_idx = cufile_desc.cufile_io_params.size();
        // ass
        LOG(INFO) << "start " << start_idx;
        batch_desc.task_list.resize(start_idx + entries.size());
        std::unordered_map<SegmentID, std::shared_ptr<SegmentDesc>> segment_desc_map;
        // segment_desc_map[LOCAL_SEGMENT_ID] = getSegmentDescByID(LOCAL_SEGMENT_ID);
        uint64_t task_id = 0, slice_id = 0;
        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            auto target_id = request.target_id;
            if (!segment_desc_map.count(target_id))
            {
                segment_desc_map[target_id] = meta_->getSegmentDescByID(target_id);
            }

            auto &desc = segment_desc_map.at(target_id);
            assert(desc->protocol == "nvmeof");
            // TODO: add mutex
            // TODO: solving iterator invalidation due to vector resize
            // Handle File Offset
            uint32_t buffer_id = 0;
            uint64_t buffer_start = request.target_offset;
            uint64_t buffer_end = request.target_id + request.length;
            uint64_t current_offset = 0;
            for (auto &buffer_desc : desc->nvmeof_buffers)
            {
                bool overlap = buffer_start < current_offset + buffer_desc.length && buffer_end > current_offset;
                if (overlap)
                {
                    // 1. get_slice_start
                    uint64_t slice_start = std::max(buffer_start, current_offset);
                    // 2. slice_end
                    uint64_t slice_end = std::min(buffer_end, current_offset + buffer_desc.length);
                    // 3. init slice and put into TransferTask
                    const char *file_path = buffer_desc.local_path_map[local_server_name_].c_str();
                    Slice *slice = new Slice();
                    slice->source_addr = (char *)request.source + slice_start - buffer_start;
                    slice->length = slice_end - slice_start;
                    slice->opcode = request.opcode;
                    slice->nvmeof.file_path = file_path;
                    slice->nvmeof.start = slice_start;
                    slice->task = &task;
                    slice->status = Slice::PENDING;
                    task.total_bytes += slice->length;
                    task.slices.push_back(slice);
                    // 4. get cufile handle
                    auto buf_key = std::make_pair(target_id, buffer_id);
                    if (!segment_to_context_.count(buf_key))
                    {
                        segment_to_context_[buf_key] = std::make_shared<CuFileContext>(file_path);
                    }
                    auto fh = segment_to_context_.at(buf_key)->getHandle();
                    // 5. add cufile request
                    CUfileIOParams_t params;
                    params.mode = CUFILE_BATCH;
                    params.fh = fh;
                    // params.fh = context_->getHandle();
                    params.opcode = request.opcode == Transport::TransferRequest::READ ? CUFILE_READ : CUFILE_WRITE;
                    params.cookie = (void *)(start_idx + task_id);
                    params.u.batch.devPtr_base = slice->source_addr;
                    params.u.batch.devPtr_offset = 0;
                    params.u.batch.file_offset = slice_start;
                    params.u.batch.size = slice_end - slice_start;

                    LOG(INFO) << "params " << "base " << request.source << " offset " << request.target_offset << " length " << request.length;

                    cufile_desc.cufile_io_params.push_back(params);
                }
                current_offset += buffer_desc.length;
            }

            cufile_desc.transfer_status.push_back(TransferStatus{.s = PENDING, .transferred_bytes = 0});
            ++task_id;
        }

        CUFILE_CHECK(cuFileBatchIOSubmit(cufile_desc.handle, entries.size(), cufile_desc.cufile_io_params.data() + start_idx, 0));
        return 0;
    }

    int NVMeoFTransport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args)
    {
        return Transport::install(local_server_name, meta, args);
    }

    int NVMeoFTransport::registerLocalMemory(void *addr, size_t length, const string &location)
    {
        return 0;
    }

    int NVMeoFTransport::unregisterLocalMemory(void *addr)
    {
        return 0;
    }

}