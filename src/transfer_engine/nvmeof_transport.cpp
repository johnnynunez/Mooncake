#include "nvmeof_transport.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transport.h"
#include <cstdint>

namespace mooncake {

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

    BatchID NVMeoFTransport::allocateBatchID(size_t batch_size) {
        // TODO: this can be moved to upper layer?
        auto batch_desc = new BatchDesc();
        if (!batch_desc)
            return -1;
        batch_desc->id = BatchID(batch_desc);
        batch_desc->batch_size = batch_size;
        batch_desc->task_list.reserve(batch_size);
        batch_desc->cufile_io_params.reserve(batch_size);
        batch_desc->cufile_events_buf.reserve(batch_size);
        // TODO: 在按照文件拆分之后再设置 size
        CUFILE_CHECK(cuFileBatchIOSetUp(&batch_desc->handle, batch_size));

#ifdef CONFIG_USE_BATCH_DESC_SET
        batch_desc_lock_.lock();
        batch_desc_set_[batch_desc->id] = batch_desc;
        batch_desc_lock_.unlock();
#endif
        return batch_desc->id;
    }

    int NVMeoFTransport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        unsigned nr = batch_desc.cufile_io_params.size() - batch_desc.nr_completed;
        CUFILE_CHECK(cuFileBatchIOGetStatus(batch_desc.handle, 0, &nr, batch_desc.cufile_events_buf.data(), NULL));
        auto &event = batch_desc.cufile_events_buf[task_id];
        unsigned idx = (intptr_t)event.cookie;
        TransferStatus transfer_status;
        transfer_status.s = from_cufile_transfer_status(event.status);
        if (transfer_status.s == COMPLETED)
        {
            transfer_status.transferred_bytes = event.ret;
        }
        batch_desc.nr_completed += 1;
        status = transfer_status;
        return 0;
    }

    int NVMeoFTransport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        // std::unordered_map<std::shared_ptr<RdmaContext>, std::vector<Slice *>> slices_to_post;
        size_t start_idx = batch_desc.task_list.size();
        batch_desc.task_list.resize(start_idx + entries.size());
        // TODO: interact with seg
        // std::unordered_map<SegmentID, std::shared_ptr<SegmentDesc>> segment_desc_map;
        // segment_desc_map[LOCAL_SEGMENT_ID] = getSegmentDescByID(LOCAL_SEGMENT_ID);
        // for (auto &request : entries)
        // {
        //     auto target_id = request.target_id;
        //     if (!segment_desc_map.count(target_id))
        //         segment_desc_map[target_id] = getSegmentDescByID(target_id);
        // }
        // TODO: part 
        uint64_t task_id = 0;
        for (auto &request : entries)
        {
            CUfileIOParams_t params;
            params.mode = CUFILE_BATCH;
            params.fh = this->segment_to_context_[request.target_id].getHandle();
            params.opcode = request.opcode == Transport::TransferRequest::READ ? CUFILE_READ : CUFILE_WRITE;
            params.cookie = (void *)(start_idx + task_id);
            params.u.batch.devPtr_base = request.source;
            params.u.batch.devPtr_offset = 0;
            params.u.batch.file_offset = request.target_offset;
            params.u.batch.size = request.length;
            batch_desc.cufile_io_params.push_back(params);
            batch_desc.transfer_status.push_back(TransferStatus{.s = PENDING, .transferred_bytes = 0});
            ++task_id;
        }

        CUFILE_CHECK(cuFileBatchIOSubmit(batch_desc.handle, entries.size(), batch_desc.cufile_io_params.data() + start_idx, 0));
        // for (auto &entry : slices_to_post)
        //     entry.first->submitPostSend(entry.second);
        return 0;
    }

    int NVMeoFTransport::freeBatchID(BatchID batch_id) {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        const size_t task_count = batch_desc.task_list.size();
        for (size_t task_id = 0; task_id < task_count; task_id++)
        {
            if (!batch_desc.task_list[task_id].is_finished)
            {
                LOG(ERROR) << "BatchID cannot be freed until all tasks are done";
                return -1;
            }
        }
        delete &batch_desc;
        return 0;
    }

    int NVMeoFTransport::install(void** args) {
        return 0;
    }

    int NVMeoFTransport::registerLocalMemory(void *addr, size_t length, const string &location) {
        return 0;
    }

    int NVMeoFTransport::unregisterLocalMemory(void *addr) {
        return 0;
    }


}