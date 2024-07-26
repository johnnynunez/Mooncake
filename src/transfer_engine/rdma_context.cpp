// rdma_context.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/worker_pool.h"

#include <sys/epoll.h>
#include <cassert>
#include <fcntl.h>
#include <atomic>
#include <thread>

namespace mooncake
{
    RdmaContext::RdmaContext(TransferEngine &engine, const std::string &device_name)
        : device_name_(device_name),
          engine_(engine),
          next_comp_channel_index_(0),
          next_comp_vector_index_(0),
          worker_pool_(nullptr)
    {
        static std::once_flag g_once_flag;
        auto fork_init = []()
        {
            if (ibv_fork_init())
                PLOG(ERROR) << "ibv_fork_init failed";
        };
        std::call_once(g_once_flag, fork_init);
    }

    RdmaContext::~RdmaContext()
    {
        if (context_)
            deconstruct();
    }

    int RdmaContext::construct(size_t num_cq_list,
                               size_t num_comp_channels,
                               uint8_t port,
                               int gid_index,
                               size_t max_cqe)
    {
        if (openRdmaDevice(device_name_, port, gid_index))
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

        // TODO 确定网卡所属的 NUMA Socket，并且填充到 WorkerPool 的第二个构造函数中
        worker_pool_ = std::make_shared<WorkerPool>(*this);
        return 0;
    }

    int RdmaContext::deconstruct()
    {
        worker_pool_.reset();
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

    uint32_t RdmaContext::rkey(void *addr)
    {
        RWSpinlock::ReadGuard guard(memory_regions_lock_);
        for (auto iter = memory_region_list_.begin(); iter != memory_region_list_.end(); ++iter)
            if ((*iter)->addr <= addr && addr < (char *)((*iter)->addr) + (*iter)->length)
                return (*iter)->rkey;

        LOG(ERROR) << "address " << addr << " rkey not found for " << deviceName();
        return 0;
    }

    uint32_t RdmaContext::lkey(void *addr)
    {
        RWSpinlock::ReadGuard guard(memory_regions_lock_);
        for (auto iter = memory_region_list_.begin(); iter != memory_region_list_.end(); ++iter)
            if ((*iter)->addr <= addr && addr < (char *)((*iter)->addr) + (*iter)->length)
                return (*iter)->lkey;

        LOG(ERROR) << "address " << addr << " lkey not found for " << deviceName();
        return 0;
    }

    std::shared_ptr<RdmaEndPoint> RdmaContext::endpoint(const std::string &peer_nic_path)
    {
        if (peer_nic_path.empty())
        {
            LOG(ERROR) << "Invalid peer nic path";
            return nullptr;
        }
        {
            RWSpinlock::ReadGuard guard(endpoint_map_lock_);
            auto iter = endpoint_map_.find(peer_nic_path);
            if (iter != endpoint_map_.end())
                return iter->second;
        }

        RWSpinlock::WriteGuard guard(endpoint_map_lock_);
        auto iter = endpoint_map_.find(peer_nic_path);
        if (iter != endpoint_map_.end())
            return iter->second;
        auto endpoint = std::make_shared<RdmaEndPoint>(*this);
        int ret = endpoint->construct(cq());
        if (ret)
            return nullptr;
        endpoint->setPeerNicPath(peer_nic_path);
        endpoint_map_[peer_nic_path] = endpoint;
        worker_pool_->insertEndPoint(endpoint);
        return endpoint;
    }

    int RdmaContext::deleteEndpoint(const std::string &peer_nic_path)
    {
        RWSpinlock::WriteGuard guard(endpoint_map_lock_);
        auto iter = endpoint_map_.find(peer_nic_path);
        if (iter != endpoint_map_.end())
        {
            worker_pool_->removeEndPoint(iter->second);
            endpoint_map_.erase(iter);
        }
        return 0;
    }

    std::string RdmaContext::nicPath() const
    {
        return MakeNicPath(engine_.local_server_name_, device_name_);
    }

    std::string RdmaContext::gid() const
    {
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

    ibv_comp_channel *RdmaContext::compChannel()
    {
        int index = (next_comp_channel_index_++) % num_comp_channel_;
        return comp_channel_[index];
    }

    int RdmaContext::compVector()
    {
        return (next_comp_vector_index_++) % context_->num_comp_vectors;
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
            active_speed_ = attr.active_speed;
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

    int RdmaContext::poll(int num_entries, ibv_wc *wc, int cq_index)
    {
        int nr_poll = ibv_poll_cq(cq_list_[cq_index], num_entries, wc);
        if (nr_poll < 0)
        {
            PLOG(ERROR) << "Failed to poll CQ #" << cq_index << " of device " << device_name_;
            return -1;
        }
        return nr_poll;
    }

    void RdmaContext::notifyWorker()
    {
        worker_pool_->notify();
    }
}