// rdma_context.h
// Copyright (C) 2024 Feng Ren

#ifndef RDMA_CONTEXT_H
#define RDMA_CONTEXT_H

#include <atomic>
#include <thread>
#include <string>
#include <condition_variable>
#include <unordered_map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <infiniband/verbs.h>

#include "transfer_engine/common.h"

namespace mooncake
{

    class RdmaEndPoint;
    class TransferEngine;

    class RdmaContext
    {
    public:
        RdmaContext(TransferEngine *engine);

        ~RdmaContext();

        int construct(const std::string &device_name,
                      size_t num_cq_list = 1,
                      size_t num_comp_channels = 1,
                      uint8_t port = 1,
                      int gid_index = 0,
                      size_t max_cqe = 256);

        int deconstruct();

        // memory region (ibv_mr *) management

        int registerMemoryRegion(void *addr, size_t length, int access);

        int unregisterMemoryRegion(void *addr);

        uint32_t rkey(void *addr);

        uint32_t lkey(void *addr);

        // endpoint management

        RdmaEndPoint *endpoint(const std::string &peer_nic_path);

        int deleteEndpoint(const std::string &peer_nic_path);

        // misc
        bool ready() const { return context_; }

        std::string deviceName() const { return device_name_; }

    public:
        uint16_t lid() const { return lid_; }

        std::string gid() const;

        int gidIndex() const { return gid_index_; }

        ibv_context *context() const { return context_; }

        TransferEngine *engine() const { return engine_; }

        ibv_pd *pd() const { return pd_; }

        uint8_t portNum() const { return port_; }

        int activeSpeed() const { return active_speed_; }

        ibv_comp_channel *compChannel();

        int compVector();

        ibv_cq *cq() const { return cq_list_[0]; }

        void notifySenderThread();

    private:
        int openRdmaDevice(const std::string &device_name, uint8_t port, int gid_index);

        int joinNonblockingPollList(int event_fd, int data_fd);

        int poll(int num_entries, ibv_wc *wc, int cq_index = 0);

        void senderAndPoller();

        void sender(int thread_id, int num_threads);

        void poller(int thread_id, int num_threads);

    private:
        std::string device_name_;
        TransferEngine *engine_;

        ibv_context *context_ = nullptr;
        ibv_pd *pd_ = nullptr;
        int event_fd_ = -1;

        size_t num_comp_channel_ = 0;
        ibv_comp_channel **comp_channel_ = nullptr;

        uint8_t port_ = 0;
        uint16_t lid_ = 0;
        int gid_index_ = -1;
        int active_speed_ = -1;
        ibv_gid gid_;

        RWSpinlock memory_regions_lock_;
        std::vector<ibv_mr *> memory_region_list_;

        int max_cqe_;
        std::vector<ibv_cq *> cq_list_;

        RWSpinlock endpoint_map_lock_;
        std::atomic<int> endpoint_map_version_;
        std::unordered_map<std::string, std::shared_ptr<RdmaEndPoint>> endpoint_map_;

        std::vector<std::thread> background_thread_;
        std::atomic<bool> threads_running_;

        std::atomic<int> next_comp_channel_index_;
        std::atomic<int> next_comp_vector_index_;

        std::mutex cond_mutex_;
        std::condition_variable cond_var_;
        std::atomic<bool> suspended_flag_;
    };

}

#endif // RDMA_CONTEXT_H
