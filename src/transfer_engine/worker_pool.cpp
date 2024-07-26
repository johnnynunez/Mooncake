// worker_pool.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/worker_pool.h"
#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"

namespace mooncake
{
    WorkerPool::WorkerPool(RdmaContext &context, int numa_socket_id)
        : context_(context),
          numa_socket_id_(numa_socket_id),
          workers_running_(true),
          suspended_flag_(false),
          endpoint_set_version_(0),
          submitted_slice_count_(0),
          posted_slice_count_(0),
          completed_slice_count_(0)
    {
        worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::worker, this)));
    }

    WorkerPool::~WorkerPool()
    {
        if (workers_running_)
        {
            cond_var_.notify_all();
            workers_running_ = false;
            for (auto &entry : worker_thread_)
                entry.join();
        }
    }

    void WorkerPool::insertEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint)
    {
        RWSpinlock::WriteGuard guard(endpoint_set_lock_);
        endpoint_set_.insert(endpoint);
        endpoint_set_version_++;
    }

    void WorkerPool::removeEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint)
    {
        RWSpinlock::WriteGuard guard(endpoint_set_lock_);
        endpoint_set_.erase(endpoint);
        endpoint_set_version_++;
    }

    int WorkerPool::submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list)
    {
        RWSpinlock::WriteGuard guard(slice_list_lock_);
        for (auto &slice : slice_list) {
            auto &peer_segment_desc = slice->peer_segment_desc;
            int buffer_id, device_id;
            TransferEngine::selectDevice(peer_segment_desc, slice->rdma.dest_addr, buffer_id, device_id);
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice_list_map_[peer_nic_path].push_back(slice);
        }
        submitted_slice_count_.fetch_add(slice_list.size(), std::memory_order_relaxed);
        return 0;
    }

    void WorkerPool::notify()
    {
        // TBD
    }

    void WorkerPool::performPostSend()
    {
        RWSpinlock::WriteGuard guard(slice_list_lock_);
        for (auto &entry : slice_list_map_) 
        {
            auto endpoint = context_.endpoint(entry.first);
            if (!endpoint->connected())
                endpoint->setupConnectionsByActive();
            std::vector<TransferEngine::Slice *> failed_slice_list;
            if (endpoint->submitPostSend(entry.second, failed_slice_list))
            {
                for (auto &slice : failed_slice_list)
                    processFailedSlice(slice);
            }
        }
    }

    void WorkerPool::performPollCq()
    {
        for (int cq_index = 0; cq_index < context_.cqCount(); ++cq_index)
        {
            ibv_wc wc[16];
            int nr_poll = context_.poll(16, wc, cq_index);
            if (nr_poll < 0)
            {
                LOG(ERROR) << "Worker: Failed to poll completion queues";
                continue;
            }

            for (int i = 0; i < nr_poll; ++i)
            {
                TransferEngine::Slice *slice = (TransferEngine::Slice *)wc[i].wr_id;
                __sync_fetch_and_sub(slice->rdma.qp_depth, 1);
                if (wc[i].status != IBV_WC_SUCCESS)
                {
                    LOG(ERROR) << "Worker: Process failed for slice (opcode: " << slice->opcode
                               << ", source_addr: " << slice->source_addr
                               << ", length: " << slice->length
                               << ", dest_addr: " << slice->rdma.dest_addr
                               << "): " << ibv_wc_status_str(wc[i].status);
                    processFailedSlice(slice);
                }
                else
                {
                    slice->status = TransferEngine::Slice::SUCCESS;
                    __sync_fetch_and_add(&slice->task->transferred_bytes, slice->length);
                    __sync_fetch_and_add(&slice->task->success_slice_count, 1);
                }
            }
        }
    }

    void WorkerPool::processFailedSlice(TransferEngine::Slice *slice)
    {
        if (slice->rdma.retry_cnt == slice->rdma.max_retry_cnt) {
            slice->status = TransferEngine::Slice::FAILED;
            __sync_fetch_and_add(&slice->task->failed_slice_count, 1);
        } else {
            slice->rdma.retry_cnt++;
            auto &peer_segment_desc = slice->peer_segment_desc;
            int buffer_id, device_id;
            TransferEngine::selectDevice(peer_segment_desc, slice->rdma.dest_addr, buffer_id, device_id);
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice_list_map_[peer_nic_path].push_back(slice);
        }
    }

    void WorkerPool::worker()
    {
        bindToSocket(numa_socket_id_);
        while (workers_running_)
        {
            performPostSend();
            performPollCq();
        }
    }
}