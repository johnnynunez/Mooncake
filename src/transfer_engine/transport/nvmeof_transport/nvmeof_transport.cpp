#include "nvmeof_transport.h"
#include "cufile_context.h"
#include "cufile_desc_pool.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transfer_metadata.h"
#include "transfer_engine/transport/transport.h"
#include <algorithm>
#include <bits/stdint-uintn.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <iomanip>
#include <memory>

namespace mooncake
{
    NVMeoFTransport::NVMeoFTransport()
    {
        LOG(INFO) << "register one handle";
        // CUFILE_CHECK(cuFileBatchIOSetUp(&handle, 8));
        LOG(INFO) << "make desc pool";
        desc_pool_ = std::make_shared<CUFileDescPool>();
        LOG(INFO) << "after make desc pool";
    }

    NVMeoFTransport::~NVMeoFTransport() {}

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

    NVMeoFTransport::BatchID NVMeoFTransport::allocateBatchID(size_t batch_size)
    {
        auto nvmeof_desc = new NVMeoFBatchDesc();
        auto batch_id = Transport::allocateBatchID(batch_size);
        auto &batch_desc = *((BatchDesc *)(batch_id));
        nvmeof_desc->desc_idx_ = desc_pool_->allocCUfileDesc(batch_size);
        nvmeof_desc->transfer_status.reserve(batch_size);
        nvmeof_desc->task_to_slices.reserve(batch_size);
        batch_desc.context = nvmeof_desc;
        return batch_id;
    }

    int NVMeoFTransport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &task = batch_desc.task_list[task_id];
        auto &nvmeof_desc = *((NVMeoFBatchDesc *)(batch_desc.context));
        // LOG(DEBUG) << "get t n " << nr;
        // 1. get task -> id map
        auto [slice_id, slice_num] = nvmeof_desc.task_to_slices[task_id];
#ifdef USE_LOCAL_DESC
        assert(slice_id == task_id);
        assert(slice_num == 1);
#endif

        // LOG(INFO) << "cufile events buf nr " << nvmeof_desc.cufile_events_buf.size();
        // for (int i = 0; i < nvmeof_desc.cufile_events_buf.size(); ++i) {
        //     LOG(INFO) << i << " status " << nvmeof_desc.cufile_events_buf[i].status <<  " ret " << nvmeof_desc.cufile_events_buf[i].ret;
        // }

        TransferStatus transfer_status = {.transferred_bytes = 0};
        for (size_t i = slice_id; i < slice_id + slice_num; ++i)
        {
            // LOG(INFO) << "task " << task_id << " i " << i << " upper bound " << slice_num;
            auto event = desc_pool_->getTransferStatus(nvmeof_desc.desc_idx_, slice_id);
            transfer_status.s = from_cufile_transfer_status(event.status);
            // TODO(FIXME): what to do if multi slices have different status?
            if (transfer_status.s == COMPLETED)
            {
                transfer_status.transferred_bytes += event.ret;
            }
            else
            {
                break;
            }
        }
        if (transfer_status.s == COMPLETED)
        {
            task.is_finished = true;
        }
        status = transfer_status;
        return 0;
    }

    int NVMeoFTransport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &nvmeof_desc = *((NVMeoFBatchDesc *)(batch_desc.context));

        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        size_t task_id = batch_desc.task_list.size();
        size_t slice_id = desc_pool_->getSliceNum(nvmeof_desc.desc_idx_);
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
                segment_desc_map[target_id] = metadata_->getSegmentDescByID(target_id);
                assert(segment_desc_map[target_id] != nullptr);
#else
                LOG_ASSERT(target_id == LOCAL_SEGMENT_ID);
                auto local_seg_desc = std::make_shared<SegmentDesc>();
                local_seg_desc->name = local_server_name_;
                local_seg_desc->protocol = "nvmeof";
                TransferMetadata::NVMeoFBufferDesc local_buffer;
                local_buffer.length = 32 * 1024 * 1024 * 1024ULL;
                local_buffer.file_path = "/mnt/data/dsf/mooncake.img";
                local_buffer.local_path_map[local_server_name_] = local_buffer.file_path;
                local_seg_desc->nvmeof_buffers.push_back(local_buffer);
                LOG_ASSERT(local_seg_desc->nvmeof_buffers.size() == 1);
                segment_desc_map[target_id] = std::move(local_seg_desc);
#endif
            }

            auto &desc = segment_desc_map.at(target_id);
            // LOG(INFO) << "desc " << desc->name << " " << desc->protocol;
            assert(desc->protocol == "NVMeoF" || desc->protocol == "nvmeof");
            // TODO: add mutex
            // TODO: solving iterator invalidation due to vector resize
            // Handle File Offset
            uint32_t buffer_id = 0;
            uint64_t buffer_start = request.target_offset;
            uint64_t buffer_end = request.target_offset + request.length;
            uint64_t current_offset = 0;
            for (auto &buffer_desc : desc->nvmeof_buffers)
            {
                LOG(INFO) << "buffer " << buffer_desc.file_path << " " << buffer_desc.length;
                for (auto &local_path : buffer_desc.local_path_map)
                {
                    LOG(INFO) << "local path " << local_path.first << " " << local_path.second;
                }
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
                    LOG(INFO) << "local name " << local_server_name_ << " file path " << file_path;
#else
                    const char *file_path = "/mnt/nvme0n1/dsf/mooncake.img";
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
                    // LOG(INFO) << "params " << "base " << request.source << " offset " << request.target_offset << " length " << request.length;

                    desc_pool_->pushParams(nvmeof_desc.desc_idx_, params);
                }
                current_offset += buffer_desc.length;
            }

            nvmeof_desc.transfer_status.push_back(TransferStatus{.s = PENDING, .transferred_bytes = 0});
            nvmeof_desc.task_to_slices.push_back({slice_id, task.slices.size()});
            ++task_id;
            slice_id += task.slices.size();
#ifdef USE_LOCAL_DESC
            LOG_ASSERT(task.slices.size() == 1);
            LOG_ASSERT(slice_id == task_id);
#endif
        }

        desc_pool_->submitBatch(nvmeof_desc.desc_idx_);
        // LOG(INFO) << "submit nr " << slice_id << " start " << start_slice_id;
        // LOG(INFO) << "After submit";
        return 0;
    }

    int NVMeoFTransport::freeBatchID(BatchID batch_id)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        auto &nvmeof_desc = *((NVMeoFBatchDesc *)(batch_desc.context));
        int desc_idx = nvmeof_desc.desc_idx_;
        int rc = Transport::freeBatchID(batch_id);
        if (rc < 0)
        {
            return -1;
        }
        desc_pool_->freeCUfileDesc(desc_idx);
        return 0;
    }

    int NVMeoFTransport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args)
    {
#ifdef USE_LOCAL_DESC
        assert(args != NULL);
        const char *file_path = (char *)args[0];
        this->segment_to_context_[{LOCAL_SEGMENT_ID, 0}] = std::make_shared<CuFileContext>(file_path);
#endif
        return Transport::install(local_server_name, meta, args);
    }

    int NVMeoFTransport::registerLocalMemory(void *addr, size_t length, const string &location, bool update_metadata)
    {
        CUFILE_CHECK(cuFileBufRegister(addr, length, 0));
        return 0;
    }

    int NVMeoFTransport::unregisterLocalMemory(void *addr, bool update_metadata)
    {
        CUFILE_CHECK(cuFileBufDeregister(addr));
        return 0;
    }
}