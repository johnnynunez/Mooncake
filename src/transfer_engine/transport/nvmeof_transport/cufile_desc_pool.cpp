#include "cufile_desc_pool.h"
#include "cufile.h"
#include "cufile_context.h"
#include "transfer_engine/transfer_engine.h"
#include <cstddef>
#include <mutex>

namespace mooncake {
    CUFileDescPool::CUFileDescPool() {
        for (size_t i = 0; i < MAX_NR_CUFILE_DESC; ++i) {
            LOG(INFO) << "Creating CUFile Batch IO Handle " << i;
            handle_[i] = NULL;
            io_params_[i].reserve(MAX_CUFILE_BATCH_SIZE);
            io_events_[i].resize(MAX_CUFILE_BATCH_SIZE);
            start_idx_[i] = 0;
            // CUFILE_CHECK(cuFileBatchIOSetUp(&handle_[i], MAX_CUFILE_BATCH_SIZE));
        }
        available_.set();
    }

    CUFileDescPool::~CUFileDescPool() {
        for (size_t i = 0; i < MAX_NR_CUFILE_DESC; ++i) {
           cuFileBatchIODestroy(handle_[i]);
        } 
    }

    int CUFileDescPool::allocCUfileDesc(size_t batch_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = available_._Find_first();
        if (idx == available_.size()) {
            // No Batch Desc Available
            return -1;
        }
        available_[idx] = 0;
        return idx;
    }

    int CUFileDescPool::pushParams(int idx, CUfileIOParams_t &io_params) {
        auto& params = io_params_[idx];
        if (params.size() >= params.capacity()) {
            return -1;
        }
        params.push_back(io_params);
        return 0;
    }

    int CUFileDescPool::submitBatch(int idx) {
        auto& params = io_params_[idx];
        CUFILE_CHECK(cuFileBatchIOSubmit(handle_[idx], params.size() - start_idx_[idx], params.data() + start_idx_[idx], 0));
        start_idx_[idx] = params.size();
        return 0;
    }

    CUfileIOEvents_t CUFileDescPool::getTransferStatus(int idx, int slice_id) {
        unsigned nr = io_params_[idx].size();
        // TODO: optimize this
        CUFILE_CHECK(cuFileBatchIOGetStatus(handle_[idx], 0, &nr, io_events_[slice_id].data(), NULL));
        return io_events_[idx][slice_id];
    }

    int CUFileDescPool::getSliceNum(int idx) {
        auto& params = io_params_[idx];
        return params.size();
    }

    int CUFileDescPool::freeCUfileDesc(int idx) {
        // std::lock_guard<std::mutex> lock(mutex_);
        available_[idx] = 1;
        io_params_[idx].clear();
        return 0;
    }
}