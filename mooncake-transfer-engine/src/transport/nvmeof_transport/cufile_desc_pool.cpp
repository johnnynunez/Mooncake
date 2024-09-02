#include "cufile.h"
#include "transport/nvmeof_transport/cufile_context.h"
#include "transport/nvmeof_transport/cufile_desc_pool.h"
#include "transfer_engine.h"
#include <atomic>
#include <bits/stdint-uintn.h>
#include <cstddef>
#include <mutex>

namespace mooncake {
    thread_local int CUFileDescPool::thread_index = -1;
    std::atomic<int> CUFileDescPool::index_counter(0); 

    CUFileDescPool::CUFileDescPool() {
        for (size_t i = 0; i < MAX_NR_CUFILE_DESC; ++i) {
            handle_[i] = NULL;
            io_params_[i].reserve(MAX_CUFILE_BATCH_SIZE);
            io_events_[i].resize(MAX_CUFILE_BATCH_SIZE);
            start_idx_[i] = 0;
            occupied_[i].store(0, std::memory_order_relaxed);
            CUFILE_CHECK(cuFileBatchIOSetUp(&handle_[i], MAX_CUFILE_BATCH_SIZE));
            LOG(INFO) << "Creating CUFile Batch IO Handle " << i << " " << handle_[i];
        }
    }

    CUFileDescPool::~CUFileDescPool() {
        for (size_t i = 0; i < MAX_NR_CUFILE_DESC; ++i) {
           cuFileBatchIODestroy(handle_[i]);
        } 
    }

    int CUFileDescPool::allocCUfileDesc(size_t batch_size) {
        if (batch_size > MAX_CUFILE_BATCH_SIZE) {
            LOG(ERROR) << "Batch Size Exceeds Max CUFile Batch Size";
            return -1;
        }
        if (thread_index == -1) {
            thread_index = index_counter.fetch_add(1);
        }
        // LOG(INFO) << "thread_index " << thread_index;
        // RWSpinlock::WriteGuard guard_(mutex_);

        int idx = thread_index % MAX_NR_CUFILE_DESC;
        uint64_t old = 0;
        if (!occupied_[idx].compare_exchange_strong(old, thread_index)) {
            LOG(INFO) << "No Batch Descriptor Available ";
            return -1;
        }
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
        // LOG(INFO) << "submit " << idx;
        CUFILE_CHECK(cuFileBatchIOSubmit(handle_[idx], params.size() - start_idx_[idx], params.data() + start_idx_[idx], 0));
        start_idx_[idx] = params.size();
        return 0;
    }

    CUfileIOEvents_t CUFileDescPool::getTransferStatus(int idx, int slice_id) {
        unsigned nr = io_params_[idx].size();
        // LOG(INFO) << " nr " << nr << " id " << slice_id << " idx " << idx << " addr " << handle_[idx];
        // TODO: optimize this & fix start
        CUFILE_CHECK(cuFileBatchIOGetStatus(handle_[idx], 0, &nr, io_events_[idx].data(), NULL));
        // if (slice_id != -1)
        return io_events_[idx][slice_id];
        // else {
        //     CUfileIOEvents_t e;
        //     e.ret = 0; e.status = CUFILE_COMPLETE;
        //     bool complete = true;
        //     for (int i = 0; i < nr; ++i) {
        //         if (io_events_[idx][i].ret != CUFILE_COMPLETE) {
        //             complete = false;
        //             break;
        //         }
        //     }
        //     if (!complete)
        //         e.status = CUFILE_WAITING;
        //     return e;
        //     // for 
        // }
    }

    int CUFileDescPool::getSliceNum(int idx) {
        auto& params = io_params_[idx];
        return params.size();
    }

    int CUFileDescPool::freeCUfileDesc(int idx) {
        occupied_[idx].store(0, std::memory_order_relaxed);
        io_params_[idx].clear();
        start_idx_[idx] = 0;
        // memset(io_events_[idx].data(), 0, io_events_[idx].size() * sizeof(CUfileIOEvents_t));
        return 0;
    }
}