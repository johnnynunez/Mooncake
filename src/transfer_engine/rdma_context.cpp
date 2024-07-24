// rdma_context.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"
#include "transfer_engine/transfer_engine.h"

#include <sys/epoll.h>
#include <cassert>
#include <fcntl.h>
#include <atomic>
#include <thread>

namespace mooncake
{

    RdmaContext::RdmaContext(TransferEngine *engine)
        : engine_(engine),
          endpoint_map_version_(0),
          threads_running_(false),
          next_comp_channel_index_(0),
          next_comp_vector_index_(0),
          suspended_flag_(false)
    {
        static std::once_flag g_once_flag;
        std::call_once(g_once_flag, []()
                       {
        if (ibv_fork_init())
            PLOG(ERROR) << "ibv_fork_init failed"; });
    }

    RdmaContext::~RdmaContext()
    {
        if (context_)
            deconstruct();
    }

    int RdmaContext::openRdmaDevice(const std::string &device_name, uint8_t port, int gid_index)
    {
        int num_devices = 0;
        struct ibv_context *context = nullptr;
        struct ibv_device **devices = ibv_get_device_list(&num_devices);
        if (!devices || num_devices <= 0)
        {
            PLOG(ERROR) << "ibv_get_device_list failed";
            return -1;
        }

        for (int i = 0; i < num_devices; ++i)
        {
            if (device_name != ibv_get_device_name(devices[i]))
                continue;

            context = ibv_open_device(devices[i]);
            if (!context)
            {
                PLOG(ERROR) << "Failed to open device " << device_name;
                ibv_free_device_list(devices);
                return -1;
            }

            ibv_port_attr attr;
            if (ibv_query_port(context, port, &attr))
            {
                PLOG(WARNING) << "Fail to query port " << port << " on " << device_name;
                ibv_close_device(context);
                ibv_free_device_list(devices);
                return -1;
            }

            if (attr.state != IBV_PORT_ACTIVE)
            {
                LOG(WARNING) << "Device " << device_name << " port not active";
                ibv_close_device(context);
                ibv_free_device_list(devices);
                return -1;
            }

            if (ibv_query_gid(context, port, gid_index, &gid_))
            {
                PLOG(WARNING) << "Device " << device_name
                              << " GID " << gid_index << " not available";
                ibv_close_device(context);
                ibv_free_device_list(devices);
                return -1;
            }

            context_ = context;
            port_ = port;
            lid_ = attr.lid;
            gid_index_ = gid_index;

            ibv_free_device_list(devices);
            return 0;
        }

        ibv_free_device_list(devices);
        LOG(ERROR) << "No matched device found: " << device_name;
        return -1;
    }

    int RdmaContext::joinNonblockingPollList(int event_fd, int data_fd)
    {
        epoll_event event;
        memset(&event, 0, sizeof(epoll_event));

        int flags = fcntl(data_fd, F_GETFL, 0);
        if (flags == -1)
        {
            PLOG(ERROR) << "Get F_GETFL failed";
            return -1;
        }
        if (fcntl(data_fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            PLOG(ERROR) << "Set F_GETFL failed";
            return -1;
        }

        event.events = EPOLLIN | EPOLLET;
        event.data.fd = data_fd;
        if (epoll_ctl(event_fd, EPOLL_CTL_ADD, event.data.fd, &event))
        {
            PLOG(ERROR) << "Failed to register data fd to epoll";
            close(event_fd);
            return -1;
        }

        return 0;
    }

    int RdmaContext::construct(const std::string &device_name,
                               size_t num_cq_list,
                               size_t num_comp_channels,
                               uint8_t port,
                               int gid_index,
                               size_t max_cqe)
    {
        device_name_ = device_name;
        max_cqe_ = max_cqe;

        if (openRdmaDevice(device_name, port, gid_index))
            return -1;

        LOG(INFO) << "RDMA device: " << context_->device->name
                  << ", LID: " << lid_
                  << ", GID: (" << gid_index_ << ") " << gid();

        pd_ = ibv_alloc_pd(context_);
        if (!pd_)
        {
            PLOG(ERROR) << "Failed to allocate pd";
            return -1;
        }

        num_comp_channel_ = num_comp_channels;
        comp_channel_ = new ibv_comp_channel *[num_comp_channels];
        for (size_t i = 0; i < num_comp_channels; ++i)
        {
            comp_channel_[i] = ibv_create_comp_channel(context_);
            if (!comp_channel_[i])
            {
                PLOG(ERROR) << "Failed to allocate comp_channel";
                return -1;
            }
        }

        event_fd_ = epoll_create1(0);
        if (event_fd_ < 0)
        {
            PLOG(ERROR) << "Failed to create epoll fd";
            return -1;
        }

        if (joinNonblockingPollList(event_fd_, context_->async_fd))
            return -1;

        for (size_t i = 0; i < num_comp_channel_; ++i)
            if (joinNonblockingPollList(event_fd_, comp_channel_[i]->fd))
                return -1;

        cq_list_.resize(num_cq_list);
        for (size_t i = 0; i < num_cq_list; ++i)
        {
            cq_list_[i] = ibv_create_cq(context_,
                                        max_cqe,
                                        this /* CQ context */,
                                        compChannel(),
                                        compVector());
            if (!cq_list_[i])
            {
                PLOG(ERROR) << "Failed to allocate completion queue";
                return -1;
            }
        }

        threads_running_ = true;
        background_thread_.emplace_back(std::thread(std::bind(&RdmaContext::senderAndPoller, this)));
        // background_thread_.emplace_back(std::thread(std::bind(&RdmaContext::sender, this, 0, 1)));
        // background_thread_.emplace_back(std::thread(std::bind(&RdmaContext::poller, this, 0, 1)));
        return 0;
    }

    int RdmaContext::deconstruct()
    {
        if (threads_running_)
        {
            cond_var_.notify_all();
            threads_running_ = false;
            for (auto &entry : background_thread_)
                entry.join();
        }

        endpoint_map_.clear();
        for (auto &entry : memory_region_list_)
            ibv_dereg_mr(entry);
        memory_region_list_.clear();

        for (size_t i = 0; i < cq_list_.size(); ++i)
            ibv_destroy_cq(cq_list_[i]);
        cq_list_.clear();

        if (event_fd_ >= 0)
        {
            close(event_fd_);
            event_fd_ = -1;
        }

        if (comp_channel_)
        {
            for (size_t i = 0; i < num_comp_channel_; ++i)
                if (comp_channel_[i])
                    ibv_destroy_comp_channel(comp_channel_[i]);
            delete[] comp_channel_;
            comp_channel_ = nullptr;
        }

        if (pd_)
        {
            ibv_dealloc_pd(pd_);
            pd_ = nullptr;
        }

        if (context_)
        {
            ibv_close_device(context_);
            context_ = nullptr;
        }

        LOG(INFO) << "Release resources of RDMA device: " << device_name_;
        return 0;
    }

    int RdmaContext::registerMemoryRegion(void *addr, size_t length, int access)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return -1;
        }

        ibv_mr *mr = ibv_reg_mr(pd_, addr, length, access);
        if (!mr)
        {
            PLOG(ERROR) << "Fail to register memory " << addr;
            return -1;
        }

        RWSpinlock::WriteGuard guard(memory_regions_lock_);
        memory_region_list_.push_back(mr);
        LOG(INFO) << "Memory region: " << addr << " -- " << (void *)((uintptr_t)addr + length)
                  << ", Device name: " << device_name_
                  << ", Length: " << length << " (" << length / 1024 / 1024 << " MB)"
                  << ", Permission: " << access << std::hex
                  << ", LKey: " << mr->lkey << ", RKey: " << mr->rkey;

        return 0;
    }

    int RdmaContext::unregisterMemoryRegion(void *addr)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return -1;
        }

        RWSpinlock::WriteGuard guard(memory_regions_lock_);
        bool has_removed;
        do
        {
            has_removed = false;
            for (auto iter = memory_region_list_.begin(); iter != memory_region_list_.end(); ++iter)
            {
                if ((*iter)->addr <= addr && addr < (char *)((*iter)->addr) + (*iter)->length)
                {
                    memory_region_list_.erase(iter);
                    has_removed = true;
                    break;
                }
            }
        } while (has_removed);
        return 0;
    }

    std::string RdmaContext::gid() const
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return "";
        }

        std::string gid_str;
        char buf[16] = {0};
        const static size_t kGidLength = 16;
        for (size_t i = 0; i < kGidLength; ++i)
        {
            sprintf(buf, "%02x", gid_.raw[i]);
            gid_str += i == 0 ? buf : std::string(":") + buf;
        }

        return gid_str;
    }

    uint32_t RdmaContext::rkey(void *addr)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return 0;
        }

        RWSpinlock::ReadGuard guard(memory_regions_lock_);
        for (auto iter = memory_region_list_.begin(); iter != memory_region_list_.end(); ++iter)
            if ((*iter)->addr <= addr && addr < (char *)((*iter)->addr) + (*iter)->length)
                return (*iter)->rkey;

        LOG(ERROR) << "address " << addr << " rkey not found for " << deviceName();
        return 0;
    }

    uint32_t RdmaContext::lkey(void *addr)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return 0;
        }

        RWSpinlock::ReadGuard guard(memory_regions_lock_);
        for (auto iter = memory_region_list_.begin(); iter != memory_region_list_.end(); ++iter)
            if ((*iter)->addr <= addr && addr < (char *)((*iter)->addr) + (*iter)->length)
                return (*iter)->lkey;

        LOG(ERROR) << "address " << addr << " lkey not found for " << deviceName();
        return 0;
    }

    RdmaEndPoint *RdmaContext::endpoint(const std::string &peer_nic_path)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return nullptr;
        }
        if (peer_nic_path.empty())
        {
            LOG(ERROR) << "Invalid peer nic path";
            return nullptr;
        }
        {
            RWSpinlock::ReadGuard guard(endpoint_map_lock_);
            auto iter = endpoint_map_.find(peer_nic_path);
            if (iter != endpoint_map_.end())
                return iter->second.get();
        }

        RWSpinlock::WriteGuard guard(endpoint_map_lock_);
        auto iter = endpoint_map_.find(peer_nic_path);
        if (iter != endpoint_map_.end())
            return iter->second.get();
        auto local_nic_path = MakeNicPath(engine_->local_server_name_, device_name_);
        auto endpoint = std::make_shared<RdmaEndPoint>(this, local_nic_path, peer_nic_path);
        int ret = endpoint->construct(cq());
        if (ret)
            return nullptr;
        endpoint_map_version_++;
        endpoint_map_[peer_nic_path] = endpoint;
        return endpoint.get();
    }

    int RdmaContext::deleteEndpoint(const std::string &peer_nic_path)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return -1;
        }

        RWSpinlock::WriteGuard guard(endpoint_map_lock_);
        auto iter = endpoint_map_.find(peer_nic_path);
        if (iter != endpoint_map_.end())
        {
            endpoint_map_version_++;
            endpoint_map_.erase(iter);
        }
        return 0;
    }

    ibv_comp_channel *RdmaContext::compChannel()
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return nullptr;
        }

        int index = (next_comp_channel_index_++) % num_comp_channel_;
        return comp_channel_[index];
    }

    int RdmaContext::compVector()
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return -1;
        }

        return (next_comp_vector_index_++) % context_->num_comp_vectors;
    }

    int RdmaContext::poll(int num_entries, ibv_wc *wc, int cq_index)
    {
        if (!ready())
        {
            LOG(ERROR) << "Context for " << deviceName() << " not ready";
            return -1;
        }

        int nr_poll = ibv_poll_cq(cq_list_[cq_index], num_entries, wc);
        if (nr_poll < 0)
        {
            PLOG(ERROR) << "Failed to poll CQ #" << cq_index << " of device " << device_name_;
            return -1;
        }
        return nr_poll;
    }

    void RdmaContext::senderAndPoller()
    {
        std::vector<RdmaEndPoint *> endpoint_list;
        int endpoint_version = -1;
        uint64_t ack_slice_count = 0;
        while (threads_running_)
        {
            if (endpoint_version != endpoint_map_version_.load(std::memory_order_relaxed))
            {
                RWSpinlock::ReadGuard guard(endpoint_map_lock_);
                endpoint_list.clear();
                for (auto &entry : endpoint_map_)
                    endpoint_list.push_back(entry.second.get());
                endpoint_version = endpoint_map_version_.load(std::memory_order_relaxed);
            }

            uint64_t post_slice_count = 0;
            for (auto &entry : endpoint_list)
                post_slice_count += entry->postSliceCount();
            if (post_slice_count == ack_slice_count)
            {
                std::unique_lock<std::mutex> lock(cond_mutex_);
                suspended_flag_.store(true, std::memory_order_release);
                // LOG(INFO) << "wait begin ! " << post_slice_count << " == " << ack_slice_count;
                cond_var_.wait(lock);
                // LOG(INFO) << "wait end ! ";
                continue;
            }

            for (auto &entry : endpoint_list)
                if (entry->performPostSend())
                    LOG(ERROR) << "Failed to send work requests";
            for (int cq_index = 0; cq_index < (int)cq_list_.size(); ++cq_index)
            {
                ibv_wc wc[16];
                int nr_poll = poll(16, wc, cq_index);
                if (nr_poll < 0)
                    LOG(ERROR) << "Failed to poll completion queues";
                ack_slice_count += nr_poll;
                for (int i = 0; i < nr_poll; ++i)
                {
                    TransferEngine::Slice *slice = (TransferEngine::Slice *)wc[i].wr_id;
                    if (wc[i].status != IBV_WC_SUCCESS)
                    {
                        slice->status.store(TransferEngine::Slice::FAILED, std::memory_order_relaxed);
                        __sync_fetch_and_add(&slice->task->failed_slice_count, 1);
                        LOG(ERROR) << "Process failed for slice (opcode: " << slice->opcode
                                   << ", source_addr: " << slice->source_addr
                                   << ", length: " << slice->length
                                   << ", dest_addr: " << slice->rdma.dest_addr
                                   << "): " << ibv_wc_status_str(wc[i].status);
                    }
                    else
                    {
                        slice->status.store(TransferEngine::Slice::SUCCESS, std::memory_order_relaxed);
                        __sync_fetch_and_add(&slice->task->success_slice_count, 1);
                        __sync_fetch_and_add(&slice->task->transferred_bytes, slice->length);
                    }
                    if (slice->rdma.qp_depth)
                        (*slice->rdma.qp_depth)--;
                }
            }
        }
    }

    void RdmaContext::notifySenderThread()
    {
        if (!suspended_flag_.load(std::memory_order_acquire))
            return;
        std::unique_lock<std::mutex> lock(cond_mutex_);
        cond_var_.notify_all();
        // LOG(INFO) << "notify!";
        suspended_flag_.store(false, std::memory_order_relaxed);
    }

    void RdmaContext::sender(int thread_id, int num_threads)
    {
        std::vector<RdmaEndPoint *> endpoint_list;
        int endpoint_version = -1;
        while (threads_running_)
        {
            if (endpoint_version != endpoint_map_version_.load(std::memory_order_relaxed))
            {
                RWSpinlock::ReadGuard guard(endpoint_map_lock_);
                endpoint_list.clear();
                for (auto &entry : endpoint_map_)
                    endpoint_list.push_back(entry.second.get());
                endpoint_version = endpoint_map_version_.load(std::memory_order_relaxed);
            }
            for (int i = thread_id; i < (int)endpoint_list.size(); i += num_threads)
            {
                if (endpoint_list[i]->performPostSend())
                    LOG(ERROR) << "Failed to send work requests";
            }
        }
    }

    void RdmaContext::poller(int thread_id, int num_threads)
    {
        while (threads_running_)
        {
            for (int cq_index = thread_id; cq_index < (int)cq_list_.size(); cq_index += num_threads)
            {
                ibv_wc wc[16];
                int nr_poll = poll(16, wc, cq_index);
                if (nr_poll < 0)
                    LOG(ERROR) << "Failed to poll completion queues";
                for (int i = 0; i < nr_poll; ++i)
                {
                    TransferEngine::Slice *slice = (TransferEngine::Slice *)wc[i].wr_id;
                    if (wc[i].status != IBV_WC_SUCCESS)
                    {
                        slice->status = TransferEngine::Slice::FAILED;
                        LOG(ERROR) << "Process failed for slice (opcode: " << slice->opcode
                                   << ", source_addr: " << slice->source_addr
                                   << ", length: " << slice->length
                                   << ", dest_addr: " << slice->rdma.dest_addr
                                   << "): " << ibv_wc_status_str(wc[i].status);
                    }
                    else
                        slice->status = TransferEngine::Slice::SUCCESS;
                    if (slice->rdma.qp_depth)
                        (*slice->rdma.qp_depth)--;
                }
            }
        }
    }

}