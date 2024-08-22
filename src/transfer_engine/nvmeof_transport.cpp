#include "nvmeof_transport.h"
#include "transfer_engine/cufile_context.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transfer_metadata.h"
#include "transfer_engine/transport.h"
#include <algorithm>
#include <bits/stdint-uintn.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <iomanip>
#include <memory>
#include <utility>

#define USE_LOCAL_DESC

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
        auto cufile_desc = new CuFileBatchDesc();
        auto batch_id = Transport::allocateBatchID(batch_size);
        auto &batch_desc = *((BatchDesc *)(batch_id));
        cufile_desc->cufile_io_params.reserve(batch_size);
        cufile_desc->cufile_events_buf.resize(batch_size);
        // TODO(FIXME): solving iterator invalidation due to vector resize
        cufile_desc->transfer_status.reserve(batch_size);
        cufile_desc->task_to_slices.reserve(batch_size);
        batch_desc.context = cufile_desc;
        CUFILE_CHECK(cuFileBatchIOSetUp(&cufile_desc->handle, batch_size));
        return batch_id;
    }

    int NVMeoFTransport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &task = batch_desc.task_list[task_id];
        auto &cufile_desc = *((CuFileBatchDesc *)(batch_desc.context));
        unsigned nr = cufile_desc.cufile_io_params.size();
        // LOG(DEBUG) << "get t n " << nr;
        // 1. get task -> id map
        auto [slice_id, slice_num] = cufile_desc.task_to_slices[task_id];
        #ifdef USE_LOCAL_DESC
        assert(slice_id == task_id);
        // LOG(TRACE) << "slice id " << slice_id << "slice number " << slice_num;
        assert(slice_num == 1);
        #endif
        CUFILE_CHECK(cuFileBatchIOGetStatus(cufile_desc.handle, 0, &nr, cufile_desc.cufile_events_buf.data(), NULL));

        TransferStatus transfer_status;
        for (size_t i = 0; i < slice_num; ++i)
        {
            auto &event = cufile_desc.cufile_events_buf[i];
            unsigned idx = (intptr_t)event.cookie;
            transfer_status.s = from_cufile_transfer_status(event.status);
            // TODO(FIXME): what to do if multi slices have different status?
            if (transfer_status.s == COMPLETED)
            {
                transfer_status.transferred_bytes += event.ret;
            } else {
                break;
            }
        }
        if (transfer_status.s == COMPLETED) {
            task.is_finished = true;
        }
        status = transfer_status;
        return 1;
    }

    int NVMeoFTransport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &cufile_desc = *((CuFileBatchDesc *)(batch_desc.context));

        // std::unordered_map<std::shared_ptr<RdmaContext>, std::vector<Slice *>> slices_to_post;
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        size_t task_id = batch_desc.task_list.size();
        size_t slice_id = cufile_desc.cufile_io_params.size();
        size_t start_slice_id = slice_id;
        batch_desc.task_list.resize(task_id + entries.size());
        std::unordered_map<SegmentID, std::shared_ptr<SegmentDesc>> segment_desc_map;
        // segment_desc_map[LOCAL_SEGMENT_ID] = getSegmentDescByID(LOCAL_SEGMENT_ID);
        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            auto target_id = request.target_id;
            if (!segment_desc_map.count(target_id))
            {
                #ifndef USE_LOCAL_DESC
                segment_desc_map[target_id] = meta_->getSegmentDescByID(target_id);
                #else
                LOG_ASSERT(target_id == LOCAL_SEGMENT_ID);
                auto local_seg_desc = std::make_shared<SegmentDesc>();
                local_seg_desc->name = local_server_name_;
                local_seg_desc->protocol = "nvmeof";
                TransferMetadata::NVMeoFBufferDesc local_buffer;
                local_buffer.length = 32 * 1024 * 1024 * 1024ULL;
                local_buffer.file_path = "/mnt/nvme0n1/dsf/mooncake.img";
                local_buffer.local_path_map[local_server_name_] = local_buffer.file_path;
                local_seg_desc->nvmeof_buffers.push_back(local_buffer);
                LOG_ASSERT(local_seg_desc->nvmeof_buffers.size() == 1);
                segment_desc_map[target_id] = std::move(local_seg_desc);
                #endif
            }

            auto &desc = segment_desc_map.at(target_id);
            assert(desc->protocol == "nvmeof");
            // TODO: add mutex
            // TODO: solving iterator invalidation due to vector resize
            // Handle File Offset
            uint32_t buffer_id = 0;
            uint64_t buffer_start = request.target_offset;
            uint64_t buffer_end = request.target_offset + request.length;
            uint64_t current_offset = 0;
            for (auto &buffer_desc : desc->nvmeof_buffers)
            {
                bool overlap = buffer_start < current_offset + buffer_desc.length && buffer_end > current_offset; // this buffer intersects with user's target
                if (overlap)
                {
                    // 1. get_slice_start
                    uint64_t slice_start = std::max(buffer_start, current_offset);
                    // 2. slice_end
                    uint64_t slice_end = std::min(buffer_end, current_offset + buffer_desc.length);
                    // 3. init slice and put into TransferTask
                    #ifndef USE_LOCAL_DESC
                    const char *file_path = buffer_desc.local_path_map[local_server_name_].c_str();
                    #else
                    const char* file_path = "/mnt/nvme0n1/dsf/mooncake.img";
                    #endif
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
                    // 5. add cufile request
                    CUfileIOParams_t params;
                    params.mode = CUFILE_BATCH;
                    params.fh = segment_to_context_.at(buf_key)->getHandle();
                    params.opcode = request.opcode == Transport::TransferRequest::READ ? CUFILE_READ : CUFILE_WRITE;
                    params.cookie = (void *)task_id;
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
            cufile_desc.task_to_slices.push_back({slice_id, task.slices.size()});
            ++task_id;
            slice_id += task.slices.size();
            #ifdef USE_LOCAL_DESC
            LOG_ASSERT(task.slices.size() == 1);
            LOG_ASSERT(slice_id == task_id);
            #endif
        }
        
        LOG(INFO) << "submit nr " << slice_id << " start " << start_slice_id;
        CUFILE_CHECK(cuFileBatchIOSubmit(cufile_desc.handle, slice_id - start_slice_id, cufile_desc.cufile_io_params.data() + start_slice_id, 0));
        LOG(INFO) << "After submit";
        return 0;
    }

    int NVMeoFTransport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args)
    {
        #ifdef USE_LOCAL_DESC
        assert(args != NULL);
        const char* file_path = (char*)args[0];
        this->segment_to_context_[{LOCAL_SEGMENT_ID, 0}] = std::make_shared<CuFileContext>(file_path);
        #endif
        return Transport::install(local_server_name, meta, args);
    }

    int NVMeoFTransport::registerLocalMemory(void *addr, size_t length, const string &location, bool remote_accessible)
    {
        CUFILE_CHECK(cuFileBufRegister(addr, length, 0));
        return 0;
    }

    int NVMeoFTransport::unregisterLocalMemory(void *addr)
    {
        CUFILE_CHECK(cuFileBufDeregister(addr));
        return 0;
    }

}