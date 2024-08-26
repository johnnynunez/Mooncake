// rdma_context.h
// Copyright (C) 2024 Feng Ren

#ifndef RDMA_CONTEXT_H
#define RDMA_CONTEXT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <infiniband/verbs.h>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "transfer_engine/common.h"
#include "transfer_engine/transfer_engine.h"

namespace mooncake
{

    class RdmaEndPoint;
    class TransferEngine;
    class WorkerPool;
    class EndpointStore;

    // RdmaContext 表示本地每个 NIC 所掌控的资源集合，具体包括 Memory Region、CQ、EndPoint（实质是 QP）等
    class RdmaContext
    {
    public:
        RdmaContext(TransferEngine &engine, const std::string &device_name);

        ~RdmaContext();

        int construct(size_t num_cq_list = 1,
                      size_t num_comp_channels = 1,
                      uint8_t port = 1,
                      int gid_index = 0,
                      size_t max_cqe = 4096,
                      int max_endpoints = 256);

    private:
        int deconstruct();

    public:
        // Memory Region 管理，负责维护当前 Context 下的 memory_region_list_ 列表
        int registerMemoryRegion(void *addr, size_t length, int access);

        int unregisterMemoryRegion(void *addr);

        uint32_t rkey(void *addr);

        uint32_t lkey(void *addr);

        bool active() const { return active_; }

        void set_active(bool flag) { active_ = flag; }

    public:
        // EndPoint 管理
        std::shared_ptr<RdmaEndPoint> endpoint(const std::string &peer_nic_path);

        int deleteEndpoint(const std::string &peer_nic_path);

    public:
        // 显示设备名称，如：mlx5_3
        std::string deviceName() const { return device_name_; }

        // 显示 NIC Path，如：optane20@mlx5_3
        std::string nicPath() const;

    public:
        // 关键参数的 Getter
        uint16_t lid() const { return lid_; }

        std::string gid() const;

        int gidIndex() const { return gid_index_; }

        ibv_context *context() const { return context_; }

        TransferEngine &engine() const { return engine_; }

        ibv_pd *pd() const { return pd_; }

        uint8_t portNum() const { return port_; }

        int activeSpeed() const { return active_speed_; }

        ibv_mtu activeMTU() const { return active_mtu_; }

        ibv_comp_channel *compChannel();

        int compVector();

        int eventFd() const { return event_fd_; }

        ibv_cq *cq();

        int cqCount() const { return cq_list_.size(); }

        int poll(int num_entries, ibv_wc *wc, int cq_index = 0);

        int socketId();

    private:
        int openRdmaDevice(const std::string &device_name, uint8_t port, int gid_index);

        int joinNonblockingPollList(int event_fd, int data_fd);

    public:
        int submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list);

    private:
        const std::string device_name_;
        TransferEngine &engine_;

        ibv_context *context_ = nullptr;
        ibv_pd *pd_ = nullptr;
        int event_fd_ = -1;

        size_t num_comp_channel_ = 0;
        ibv_comp_channel **comp_channel_ = nullptr;

        uint8_t port_ = 0;
        uint16_t lid_ = 0;
        int gid_index_ = -1;
        int active_speed_ = -1;
        ibv_mtu active_mtu_;
        ibv_gid gid_;

        RWSpinlock memory_regions_lock_;
        std::vector<ibv_mr *> memory_region_list_;
        std::vector<ibv_cq *> cq_list_;

        std::shared_ptr<EndpointStore> endpoint_store_;

        std::vector<std::thread> background_thread_;
        std::atomic<bool> threads_running_;

        std::atomic<int> next_comp_channel_index_;
        std::atomic<int> next_comp_vector_index_;
        std::atomic<int> next_cq_list_index_;

        std::shared_ptr<WorkerPool> worker_pool_;

        volatile bool active_;
    };

}

#endif // RDMA_CONTEXT_H
