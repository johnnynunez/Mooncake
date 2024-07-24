// transfer_engine.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"

#include <set>
#include <cassert>
#include <sys/time.h>
#include <sys/mman.h>
#include <glog/logging.h>

namespace mooncake
{

    static void *allocate_memory_pool(size_t size)
    {
        void *start_addr;
        start_addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE,
                          -1, 0);
        if (start_addr == MAP_FAILED)
        {
            PLOG(ERROR) << "Failed to allocate memory";
            return nullptr;
        }
        return start_addr;
    }

    static void free_memory_pool(void *addr, size_t size)
    {
        munmap(addr, size);
    }

    TransferEngine::TransferEngine(std::unique_ptr<TransferMetadata> &metadata,
                                   const std::string &local_server_name,
                                   size_t dram_buffer_size,
                                   size_t vram_buffer_size,
                                   const std::string &nic_priority_matrix)

        : metadata_(std::move(metadata)),
          next_segment_id_(0),
          local_server_name_(local_server_name),
          dram_buffer_size_(dram_buffer_size),
          vram_buffer_size_(vram_buffer_size)
    {
        if (allocateInternalBuffer())
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: memory pool allocation failure";
            exit(EXIT_FAILURE);
        }

        int ret = parseNicPriorityMatrix(nic_priority_matrix);
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot parse nic priority matrix";
            exit(EXIT_FAILURE);
        }

        ret = initializeRdmaResources();
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot initialize rdma resources";
            exit(EXIT_FAILURE);
        }

        ret = startHandshakeDaemon();
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot start handshake daemon";
            exit(EXIT_FAILURE);
        }

        ret = joinCluster();
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot publish segments";
            exit(EXIT_FAILURE);
        }
    }

    TransferEngine::~TransferEngine()
    {
        leaveCluster();
        connected_server_set_.clear();
        segment_desc_map_.clear();
        segment_lookup_map_.clear();
        batch_desc_set_.clear();
        context_list_.clear();
        freeInternalBuffer();
    }

    int TransferEngine::joinCluster()
    {
        TransferMetadata::ServerDesc desc;
        desc.name = local_server_name_;
        for (auto &entry : context_list_)
        {
            TransferMetadata::DeviceDesc device_desc;
            device_desc.name = entry->deviceName();
            device_desc.lid = entry->lid();
            device_desc.gid = entry->gid();
            desc.devices.push_back(device_desc);
        }

        int dram_buffer_index = 0;
        for (auto &dram_buffer : dram_buffer_list_)
        {
            TransferMetadata::SegmentDesc segment_desc;
            segment_desc.name = local_server_name_ + "/cpu:" + std::to_string(dram_buffer_index);
            segment_desc.addr = (uint64_t)dram_buffer;
            segment_desc.length = dram_buffer_size_;
            for (auto &entry : context_list_)
                segment_desc.rkey.push_back(entry->rkey(dram_buffer));
            desc.segments.push_back(segment_desc);
            ++dram_buffer_index;
        }

        // TODO VRAM

        return metadata_->broadcastServerDesc(local_server_name_, desc);
    }

    int TransferEngine::leaveCluster()
    {
        return metadata_->removeServerDesc(local_server_name_);
    }

    static std::string get_server_name(const std::string &segment_path)
    {
        size_t pos = segment_path.find('/');
        if (pos == segment_path.npos)
            return "";
        return segment_path.substr(0, pos);
    }

    TransferEngine::SegmentID TransferEngine::getSegmentID(const std::string &segment_path)
    {
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            if (segment_lookup_map_.count(segment_path))
                return segment_lookup_map_[segment_path];
        }

        RWSpinlock::WriteGuard guard(segment_lock_);
        if (segment_lookup_map_.count(segment_path))
            return segment_lookup_map_[segment_path];
        auto server_desc = metadata_->getServerDesc(get_server_name(segment_path));
        for (auto &segment : server_desc->segments)
        {
            if (segment.name == segment_path)
            {
                SegmentID id = next_segment_id_.fetch_add(1);
                segment_desc_map_[id] = &segment;
                segment_lookup_map_[segment_path] = id;
                LOG(INFO) << "Register segment id: " << id << ", addr: " << segment.addr;
                return id;
            }
        }

        return -1;
    }

    int TransferEngine::registerLocalMemory(void *addr, size_t size)
    {
        const static int access_rights = IBV_ACCESS_LOCAL_WRITE;
        for (auto &entry : context_list_)
        {
            int ret = entry->registerMemoryRegion(addr, size, access_rights);
            if (ret)
                return -1;
        }
        return 0;
    }

    int TransferEngine::unregisterLocalMemory(void *addr)
    {
        for (auto &entry : context_list_)
        {
            int ret = entry->unregisterMemoryRegion(addr);
            if (ret)
                return -1;
        }
        return 0;
    }

    TransferEngine::BatchID TransferEngine::allocateBatchID(size_t batch_size)
    {
        auto batch_desc = new BatchDesc();
        batch_desc->id = BatchID(batch_desc);
        batch_desc->batch_size = batch_size;
        batch_desc->task_list.reserve(batch_size);
        batch_desc_lock_.WLock();
        batch_desc_set_.insert(batch_desc);
        batch_desc_lock_.WUnlock();
        return BatchID(batch_desc);
    }

    int TransferEngine::submitTransfer(BatchID batch_id,
                                       const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        struct SegmentInfo4Slice
        {
            SegmentDesc *desc;
            std::string server_name;
            std::map<RdmaContext *, RdmaEndPoint *> endpoint_map;
        };

        std::unordered_map<SegmentID, SegmentInfo4Slice> segment_map;
        std::unordered_map<RdmaEndPoint *, std::vector<Slice *>> slices_to_post;
        for (auto &request : entries)
        {
            if (segment_map.count(request.target_id))
                continue;

            segment_lock_.RLock();
            if (!segment_desc_map_.count(request.target_id))
            {
                segment_lock_.RUnlock();
                LOG(ERROR) << "Invalid target id";
                return -1;
            }
            auto &info = segment_map[request.target_id];
            info.desc = segment_desc_map_[request.target_id];
            segment_lock_.RUnlock();
            info.server_name = get_server_name(info.desc->name);

            // Start handshake if needed
            if (connectServer(info.server_name))
                return -1;
        }

        size_t task_id = batch_desc.task_list.size();
        batch_desc.task_list.resize(task_id + entries.size());
        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            ++task_id;
            auto &segment_info = segment_map[request.target_id];
            const static size_t kBlockSize = 65536;
            for (uint64_t offset = 0; offset < request.length; offset += kBlockSize)
            {
                int rnic_index = rnic_prob_list_[lrand48() % 64];
                auto context = context_list_[rnic_index];
                auto &endpoint = segment_info.endpoint_map[context.get()];
                if (!endpoint)
                    endpoint = context->endpoint(segment_info.server_name);

                auto slice = new Slice();
                slice->source_addr = (char *)request.source + offset;
                slice->length = std::min(request.length - offset, kBlockSize);
                slice->opcode = request.opcode;
                slice->rdma.dest_addr = segment_info.desc->addr + request.target_offset + offset;
                slice->rdma.dest_rkey = segment_info.desc->rkey[rnic_index];
                slice->rdma.source_lkey = context->lkey(slice->source_addr);
                slice->task = &task;
                slice->status.store(Slice::PENDING, std::memory_order_relaxed);
                task.total_bytes += slice->length;
                task.slices.push_back(slice);
                slices_to_post[endpoint].push_back(slice);
            }
        }
        for (auto &entry : slices_to_post)
            entry.first->submitPostSend(entry.second);
        return 0;
    }

    int TransferEngine::getTransferStatus(BatchID batch_id,
                                          std::vector<TransferStatus> &status)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        const size_t task_count = batch_desc.task_list.size();
        status.resize(task_count);
        for (size_t task_id = 0; task_id < task_count; task_id++)
        {
            auto &task = batch_desc.task_list[task_id];
            status[task_id].transferred_bytes = task.transferred_bytes;
            uint64_t success_slice_count = task.success_slice_count;
            uint64_t failed_slice_count = task.failed_slice_count;
            if (success_slice_count + failed_slice_count == (uint64_t)task.slices.size())
            {
                if (failed_slice_count)
                    status[task_id].s = TransferStatusEnum::FAILED;
                else
                    status[task_id].s = TransferStatusEnum::COMPLETED;
            }
            else
            {
                status[task_id].s = TransferStatusEnum::WAITING;
            }
        }
        return 0;
    }

    int TransferEngine::freeBatchID(BatchID batch_id)
    {
        RWSpinlock::WriteGuard guard(batch_desc_lock_);
        auto batch_desc_ptr = ((BatchDesc *)(batch_id));
        batch_desc_set_.erase(batch_desc_ptr);
        delete batch_desc_ptr; // TODO 在任务没有完全结束前不能实际 delete 操作
        return 0;
    }

    int TransferEngine::onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        int device_index = 0;
        local_desc.server_name = local_server_name_;
        auto server_desc = metadata_->getServerDesc(peer_desc.server_name);
        if (server_desc->devices.size() != peer_desc.devices.size())
        {
            LOG(ERROR) << "NICs in local/remote server must be the same";
            return -1;
        }
        for (auto &entry : context_list_)
        {
            TransferMetadata::HandShakeDescImpl local_device_desc;
            local_device_desc.name = entry->deviceName();
            auto &peer_device_desc = peer_desc.devices[device_index];
            auto endpoint = entry->endpoint(peer_desc.server_name);
            if (endpoint == nullptr)
            {
                LOG(ERROR) << "Cannot allocate endpoint objects";
                return -1;
            }

            if (endpoint->connected())
            {
                LOG(WARNING) << "Endpoint to " << endpoint->serverName() << " will be reconnected";
                endpoint->reset();
            }

            auto peer_gid = server_desc->devices[device_index].gid;
            auto peer_lid = server_desc->devices[device_index].lid;
            int ret = endpoint->setupConnection(peer_gid, peer_lid, peer_device_desc.qp_num);
            if (ret)
                return -1; // TODO handle errors gracefully
            for (int qp_index = 0; qp_index < (int)peer_device_desc.qp_num.size(); ++qp_index)
                local_device_desc.qp_num.push_back(endpoint->qpNum(qp_index));
            device_index++;
            local_desc.devices.push_back(local_device_desc);
        }
        return 0;
    }

    int TransferEngine::parseNicPriorityMatrix(const std::string &nic_priority_matrix)
    {
        return metadata_->parseNicPriorityMatrix(nic_priority_matrix, priority_map_, rnic_list_);
    }

    int TransferEngine::initializeRdmaResources()
    {
        if (rnic_list_.empty())
        {
            LOG(ERROR) << "No available RNIC!";
            return -1;
        }

        for (auto &rnic : rnic_list_)
        {
            auto context = std::make_shared<RdmaContext>();
            if (context->construct(rnic))
                return -1;

            const static int access_rights = IBV_ACCESS_LOCAL_WRITE |
                                             IBV_ACCESS_REMOTE_WRITE |
                                             IBV_ACCESS_REMOTE_READ;

            int ret = context->registerMemoryRegion(dram_buffer_list_[0],
                                                    dram_buffer_size_,
                                                    access_rights);
            if (ret)
            {
                context->deconstruct();
                return ret;
            }

            context_list_.push_back(context);
        }

        rnic_prob_list_.resize(64, 0);
        for (size_t i = 0; i < rnic_prob_list_.size(); ++i)
            rnic_prob_list_[i] = i % rnic_list_.size();

        return 0;
    }

    int TransferEngine::startHandshakeDaemon()
    {
        return metadata_->startHandshakeDaemon(
            std::bind(&TransferEngine::onSetupRdmaConnections,
                      this,
                      std::placeholders::_1,
                      std::placeholders::_2));
    }

    int TransferEngine::connectServer(const std::string &peer_server_name)
    {
        {
            RWSpinlock::ReadGuard guard(connected_server_lock_);
            if (connected_server_set_.count(peer_server_name))
                return 0;
        }

        RWSpinlock::WriteGuard guard(connected_server_lock_);
        if (connected_server_set_.count(peer_server_name))
            return 0;

        TransferMetadata::HandShakeDesc local_desc, peer_desc;
        local_desc.server_name = local_server_name_;
        for (auto &context : context_list_)
        {
            TransferMetadata::HandShakeDescImpl entry;
            entry.name = context->deviceName();
            auto endpoint = context->endpoint(peer_server_name);
            entry.qp_num = endpoint->qpNum();
            local_desc.devices.push_back(entry);
        }

        int rc = metadata_->sendHandshake(peer_server_name, local_desc, peer_desc);
        if (rc)
            return rc;

        int device_index = 0;
        auto server_desc = metadata_->getServerDesc(peer_desc.server_name);
        for (auto &context : context_list_)
        {
            auto peer_gid = server_desc->devices[device_index].gid;
            auto peer_lid = server_desc->devices[device_index].lid;
            auto &entry = peer_desc.devices[device_index];

            entry.name = context->deviceName();
            auto endpoint = context->endpoint(peer_server_name);
            rc = endpoint->setupConnection(peer_gid, peer_lid, entry.qp_num);
            if (rc)
                return rc;
            device_index++;
        }

        connected_server_set_.insert(peer_server_name);
        return 0;
    }

    int TransferEngine::allocateInternalBuffer()
    {
        for (int socket_id = 0; socket_id < 1; ++socket_id)
        {
            auto dram_buffer = allocate_memory_pool(dram_buffer_size_);
            if (!dram_buffer)
                return -1;
            LOG(INFO) << "Allocate DRAM pool " << dram_buffer << " on socket " << socket_id;
            dram_buffer_list_.push_back(dram_buffer);
        }
        // TODO allocate CUDA vram buffer
        return 0;
    }

    int TransferEngine::freeInternalBuffer()
    {
        for (int socket_id = 0; socket_id < 1; ++socket_id)
            free_memory_pool(dram_buffer_list_[socket_id], dram_buffer_size_);
        dram_buffer_list_.clear();
        // TODO free CUDA vram buffer
        return 0;
    }

    int TransferEngine::UpdateRnicLinkSpeed(const std::vector<int> &rnic_speed)
    {
        if (rnic_speed.size() != rnic_list_.size())
            return -1;

        std::vector<double> cdf;
        double sum = 0;
        for (int value : rnic_speed)
            sum += value;

        cdf.push_back(rnic_speed[0] / sum);
        for (size_t i = 1; i < rnic_speed.size(); ++i)
            cdf.push_back(cdf[i - 1] + rnic_speed[i] / sum);

        for (size_t i = 0; i < rnic_prob_list_.size(); ++i)
        {
            double ratio = (lrand48() % 100) / 100.0;
            auto it = std::lower_bound(cdf.begin(), cdf.end(), ratio);
            rnic_prob_list_[i] = it - cdf.begin();
        }

        return 0;
    }

}