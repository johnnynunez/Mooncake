// worker_pool.cpp
// Copyright (C) 2024 Feng Ren

#include <sys/epoll.h>

#include "transfer_engine/config.h"
#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/worker_pool.h"

namespace mooncake
{

    const static int kTransferWorkerCount = globalConfig().workers_per_ctx;

    WorkerPool::WorkerPool(RdmaContext &context, int numa_socket_id)
        : context_(context),
          numa_socket_id_(numa_socket_id),
          workers_running_(true),
          suspended_flag_(0),
          submitted_slice_count_(0),
          processed_slice_count_(0)
    {
        for (int i = 0; i < kShardCount; ++i)
            slice_list_size_[i].store(0, std::memory_order_relaxed);
        for (int i = 0; i < kTransferWorkerCount; ++i)
            worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::transferWorker, this, i)));
        worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::monitorWorker, this)));
    }

    WorkerPool::~WorkerPool()
    {
        if (workers_running_)
        {
            cond_var_.notify_all();
            workers_running_.store(false);
            for (auto &entry : worker_thread_)
                entry.join();
        }
    }

    int WorkerPool::submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list)
    {
        std::unordered_map<std::string, SliceList> slice_list_map;
        std::unordered_map<SegmentID, std::shared_ptr<TransferEngine::SegmentDesc>> segment_desc_map;
        for (auto &slice : slice_list)
        {
            auto target_id = slice->target_id;
            if (!segment_desc_map.count(target_id))
                segment_desc_map[target_id] = context_.engine().getSegmentDescByID(target_id);
        }

        for (auto &slice : slice_list)
        {
            auto &peer_segment_desc = segment_desc_map[slice->target_id];
            int buffer_id, device_id;
            if (TransferEngine::selectDevice(peer_segment_desc.get(), slice->rdma.dest_addr, slice->length, buffer_id, device_id))
            {
                peer_segment_desc = context_.engine().getSegmentDescByID(slice->target_id, true);
                if (TransferEngine::selectDevice(peer_segment_desc.get(), slice->rdma.dest_addr, slice->length, buffer_id, device_id))
                {
                    LOG(ERROR) << "Failed to select remote NIC for address " << (void *) slice->rdma.dest_addr;
                    slice->markFailed();
                    continue;
                }
            }
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice->peer_nic_path = peer_nic_path;
            slice_list_map[peer_nic_path].push_back(slice);
        }

        uint64_t submitted_slice_count = 0;
        for (auto &entry : slice_list_map)
        {
            int index = std::hash<std::string>{}(entry.first) % kShardCount;
            slice_list_lock_[index].lock();
            auto &target = slice_list_map_[index][entry.first];
            target.reserve(target.size() + entry.second.size());
            for (auto &slice : entry.second)
                target.push_back(slice);
            submitted_slice_count += entry.second.size();
            slice_list_size_[index].fetch_add(entry.second.size(), std::memory_order_relaxed);
            slice_list_lock_[index].unlock();
        }

        submitted_slice_count_.fetch_add(submitted_slice_count, std::memory_order_relaxed);
        if (suspended_flag_.load(std::memory_order_relaxed))
            cond_var_.notify_all();

        return 0;
    }

    void WorkerPool::performPostSend(int thread_id)
    {
        for (int shard_id = thread_id; shard_id < kShardCount; shard_id += kTransferWorkerCount)
        {
            auto slice_list_size = 0;
            if (slice_list_size_[shard_id].load(std::memory_order_relaxed) == 0 || !slice_list_lock_[shard_id].tryLock())
                continue;

            SliceList failed_slice_list;
            for (auto &entry : slice_list_map_[shard_id])
            {
                if (entry.second.empty())
                    continue;
                
                if (entry.second[0]->target_id == LOCAL_SEGMENT_ID)
                {
                    for (auto &slice : entry.second)
                    {
                        LOG_ASSERT(slice->target_id == LOCAL_SEGMENT_ID);
                        memcpy((void *) slice->rdma.dest_addr, slice->source_addr, slice->length);
                        slice->markSuccess();
                    }
                    processed_slice_count_.fetch_add(entry.second.size());
                    entry.second.clear();
                    continue;
                }

                auto endpoint = context_.endpoint(entry.first);
                if (!endpoint)
                {
                    LOG(ERROR) << "Cannot allocate endpoint: " << entry.first;
                    for (auto &slice : entry.second)
                        failed_slice_list.push_back(slice);
                    entry.second.clear();
                    continue;
                }
                if (!endpoint->connected() && endpoint->setupConnectionsByActive())
                {
                    LOG(ERROR) << "Cannot make connection for endpoint: " << entry.first;
                    for (auto &slice : entry.second)
                        failed_slice_list.push_back(slice);
                    entry.second.clear();
                    continue;
                }
#ifdef USE_FAKE_POST_SEND
                for (auto &slice : entry.second)
                    slice->markSuccess();
                processed_slice_count_.fetch_add(entry.second.size());
                entry.second.clear();
#else
                endpoint->submitPostSend(entry.second, failed_slice_list);
                slice_list_size += entry.second.size();
#endif
            }
            if (slice_list_size)
                slice_list_size_[shard_id].store(slice_list_size, std::memory_order_relaxed);
            slice_list_lock_[shard_id].unlock();
            for (auto &slice : failed_slice_list)
                processFailedSlice(slice, thread_id);
        }
    }

    void WorkerPool::performPollCq(int thread_id)
    {
        int processed_slice_count = 0;
        const static size_t kPollCount = 64;
        std::unordered_map<volatile int *, int> qp_depth_set;
        for (int cq_index = thread_id; cq_index < context_.cqCount(); cq_index += kTransferWorkerCount)
        {
            ibv_wc wc[kPollCount];
            int nr_poll = context_.poll(kPollCount, wc, cq_index);
            if (nr_poll < 0)
            {
                LOG(ERROR) << "Worker: Failed to poll completion queues";
                continue;
            }

            for (int i = 0; i < nr_poll; ++i)
            {
                TransferEngine::Slice *slice = (TransferEngine::Slice *)wc[i].wr_id;
                if (qp_depth_set.count(slice->rdma.qp_depth))
                    qp_depth_set[slice->rdma.qp_depth]++;
                else
                    qp_depth_set[slice->rdma.qp_depth] = 1;
                // __sync_fetch_and_sub(slice->rdma.qp_depth, 1);
                if (wc[i].status != IBV_WC_SUCCESS)
                {
                    LOG(ERROR) << "Worker: Process failed for slice (opcode: " << slice->opcode
                               << ", source_addr: " << slice->source_addr
                               << ", length: " << slice->length
                               << ", dest_addr: " << slice->rdma.dest_addr
                               << "): " << ibv_wc_status_str(wc[i].status);
                    context_.deleteEndpoint(slice->peer_nic_path);
                    processFailedSlice(slice, thread_id);
                }
                else
                {
                    slice->markSuccess();
                    processed_slice_count++;
                }
            }
        }

        for (auto &entry : qp_depth_set)
            __sync_fetch_and_sub(entry.first, entry.second);

        if (processed_slice_count)
            processed_slice_count_.fetch_add(processed_slice_count);
    }

    void WorkerPool::processFailedSlice(TransferEngine::Slice *slice, int thread_id)
    {
        if (slice->rdma.retry_cnt == slice->rdma.max_retry_cnt)
        {
            slice->markFailed();
            processed_slice_count_++;
        }
        else
        {
            slice->rdma.retry_cnt++;
            auto peer_segment_desc = context_.engine().getSegmentDescByID(slice->target_id, true);
            int buffer_id, device_id;
            if (TransferEngine::selectDevice(peer_segment_desc.get(), slice->rdma.dest_addr, slice->length, buffer_id, device_id, slice->rdma.retry_cnt))
            {
                slice->markFailed();
                processed_slice_count_++;
                return;
            }
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice->peer_nic_path = peer_nic_path;

            slice_list_lock_[0].lock();
            slice_list_map_[0][peer_nic_path].push_back(slice);
            slice_list_size_[0].fetch_add(1, std::memory_order_relaxed);
            slice_list_lock_[0].unlock();

            if (globalConfig().verbose)
                LOG(INFO) << "Retry transmission to " << peer_nic_path << " for " << slice->rdma.retry_cnt << "-th attempt";
        }
    }

    void WorkerPool::transferWorker(int thread_id)
    {
        bindToSocket(numa_socket_id_);
        const static uint64_t kWaitPeriodInNano = 100000000; // 100ms
        uint64_t last_wait_ts = getCurrentTimeInNano();
        while (workers_running_.load(std::memory_order_relaxed))
        {
            auto processed_slice_count = processed_slice_count_.load(std::memory_order_relaxed);
            auto submitted_slice_count = submitted_slice_count_.load(std::memory_order_relaxed);
            if (processed_slice_count == submitted_slice_count)
            {
                uint64_t curr_wait_ts = getCurrentTimeInNano();
                if (curr_wait_ts - last_wait_ts > kWaitPeriodInNano)
                {
                    std::unique_lock<std::mutex> lock(cond_mutex_);
                    suspended_flag_.fetch_add(1);
                    cond_var_.wait_for(lock, std::chrono::seconds(1));
                    suspended_flag_.fetch_sub(1);
                    last_wait_ts = curr_wait_ts;
                }
                continue;
            }
            performPostSend(thread_id);
#ifndef USE_FAKE_POST_SEND
            performPollCq(thread_id);
#endif
        }
    }

    int WorkerPool::doProcessContextEvents()
    {
        ibv_async_event event;
        if (ibv_get_async_event(context_.context(), &event) < 0)
            return -1;
        LOG(ERROR) << "Received context async event: " << ibv_event_type_str(event.event_type) 
                   << " for context " << context_.deviceName() << ". It will be inactive.";
        context_.inactive();
        ibv_ack_async_event(&event);
        return 0;
    }

    void WorkerPool::monitorWorker()
    {
        bindToSocket(numa_socket_id_);
        while (workers_running_)
        {
            struct epoll_event event;
            int num_events = epoll_wait(context_.eventFd(), &event, 1, 100);
            if (num_events < 0)
            {
                PLOG(ERROR) << "Failed to call epoll wait";
                continue;
            }

            if (num_events == 0)
                continue;

            LOG(ERROR) << "Received event, fd: " << event.data.fd
                       << ", events: " << event.events;

            if (!(event.events & EPOLLIN))
                continue;

            if (event.data.fd == context_.context()->async_fd)
                doProcessContextEvents();
        }
    }

    int WorkerPool::getShardId()
    {
        static std::atomic<int> g_next_thread_index(0);
        thread_local int tl_index = g_next_thread_index.fetch_add(1) % kShardCount;
        return tl_index;
    }
}