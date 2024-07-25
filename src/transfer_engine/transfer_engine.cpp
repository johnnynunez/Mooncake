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
    TransferEngine::TransferEngine(std::unique_ptr<TransferMetadata> &metadata,
                                   const std::string &local_server_name,
                                   const std::string &nic_priority_matrix)
        : metadata_(std::move(metadata)),
          next_segment_id_(0),
          local_server_name_(local_server_name)
    {
        int ret = metadata_->parseNicPriorityMatrix(nic_priority_matrix, local_priority_matrix_, rnic_list_);
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

        ret = updateLocalSegmentDesc();
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot publish segments";
            exit(EXIT_FAILURE);
        }
    }

    TransferEngine::~TransferEngine()
    {
        removeLocalSegmentDesc();
        segment_id_to_desc_map_.clear();
        segment_name_to_id_map_.clear();
        batch_desc_set_.clear();
        context_list_.clear();
    }

    int TransferEngine::updateLocalSegmentDesc()
    {
        TransferMetadata::SegmentDesc desc;
        desc.name = local_server_name_;
        for (auto &entry : context_list_)
        {
            TransferMetadata::DeviceDesc device_desc;
            device_desc.name = entry->deviceName();
            device_desc.lid = entry->lid();
            device_desc.gid = entry->gid();
            desc.devices.push_back(device_desc);
        }

        {
            RWSpinlock::ReadGuard guard(registered_buffer_lock_);
            for (auto &entry : registered_buffer_list_)
            {
                TransferMetadata::BufferDesc buffer_desc;
                buffer_desc.name = entry.name;
                buffer_desc.addr = (uint64_t)entry.addr;
                buffer_desc.length = entry.length;
                buffer_desc.rkey = entry.rkey;
                desc.buffers.push_back(buffer_desc);
            }
        }

        desc.priority_matrix = local_priority_matrix_;
        return metadata_->updateSegmentDesc(local_server_name_, desc);
    }

    int TransferEngine::removeLocalSegmentDesc()
    {
        return metadata_->removeSegmentDesc(local_server_name_);
    }

    TransferEngine::SegmentID TransferEngine::getSegmentID(const std::string &segment_name)
    {
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            if (segment_name_to_id_map_.count(segment_name))
                return segment_name_to_id_map_[segment_name];
        }

        RWSpinlock::WriteGuard guard(segment_lock_);
        if (segment_name_to_id_map_.count(segment_name))
            return segment_name_to_id_map_[segment_name];
        auto server_desc = metadata_->getSegmentDesc(segment_name);
        if (!server_desc)
            return -1;
        SegmentID id = next_segment_id_.fetch_add(1);
        segment_id_to_desc_map_[id] = server_desc;
        segment_name_to_id_map_[segment_name] = id;
        return id;
    }

    std::shared_ptr<TransferEngine::SegmentDesc> TransferEngine::getSegmentDescByName(const std::string &segment_name, bool force_update)
    {
        if (!force_update)
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            auto iter = segment_name_to_id_map_.find(segment_name);
            if (iter != segment_name_to_id_map_.end())
                return segment_id_to_desc_map_[iter->second];
        }

        RWSpinlock::WriteGuard guard(segment_lock_);
        auto iter = segment_name_to_id_map_.find(segment_name);
        SegmentID segment_id;
        if (iter != segment_name_to_id_map_.end())
            segment_id = iter->second;
        else
            segment_id = next_segment_id_.fetch_add(1);
        auto server_desc = metadata_->getSegmentDesc(segment_name);
        if (!server_desc)
            return nullptr;
        segment_id_to_desc_map_[segment_id] = server_desc;
        segment_name_to_id_map_[segment_name] = segment_id;
        return server_desc;
    }

    std::shared_ptr<TransferEngine::SegmentDesc> TransferEngine::getSegmentDescByID(SegmentID segment_id, bool force_update)
    {
        if (force_update)
        {
            RWSpinlock::WriteGuard guard(segment_lock_);
            if (!segment_id_to_desc_map_.count(segment_id))
                return nullptr;
            auto server_desc = metadata_->getSegmentDesc(segment_id_to_desc_map_[segment_id]->name);
            if (!server_desc)
                return nullptr;
            segment_id_to_desc_map_[segment_id] = server_desc;
            return segment_id_to_desc_map_[segment_id];
        }
        else
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            if (!segment_id_to_desc_map_.count(segment_id))
                return nullptr;
            return segment_id_to_desc_map_[segment_id];
        }
    }

    int TransferEngine::registerLocalMemory(void *addr, size_t length, const std::string &name)
    {
        LocalBufferDesc desc;
        desc.name = name;
        desc.addr = addr;
        desc.length = length;
        const static int access_rights = IBV_ACCESS_LOCAL_WRITE |
                                         IBV_ACCESS_REMOTE_WRITE |
                                         IBV_ACCESS_REMOTE_READ;
        for (auto &entry : context_list_)
        {
            int ret = entry->registerMemoryRegion(addr, length, access_rights);
            if (ret)
                return -1;
            desc.lkey.push_back(entry->lkey(addr));
            desc.rkey.push_back(entry->rkey(addr));
        }
        {
            RWSpinlock::WriteGuard guard(registered_buffer_lock_);
            registered_buffer_list_.push_back(desc);
        }
        return updateLocalSegmentDesc();
    }

    int TransferEngine::unregisterLocalMemory(void *addr)
    {
        {
            RWSpinlock::WriteGuard guard(registered_buffer_lock_);
            for (auto iter = registered_buffer_list_.begin();
                 iter != registered_buffer_list_.end();
                 ++iter)
            {
                if (iter->addr == addr)
                {
                    for (auto &entry : context_list_)
                        entry->unregisterMemoryRegion(addr);
                    registered_buffer_list_.erase(iter);
                    break;
                }
            }
        }

        return updateLocalSegmentDesc();
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

        std::unordered_map<RdmaEndPoint *, std::vector<Slice *>> slices_to_post;
        size_t task_id = batch_desc.task_list.size();
        batch_desc.task_list.resize(task_id + entries.size());

        struct SegmentCache
        {
            std::vector<RdmaEndPoint *> endpoints;
            std::shared_ptr<SegmentDesc> desc;
        };

        std::unordered_map<SegmentID, SegmentCache> segment_cache;
        for (auto &request : entries)
        {
            if (segment_cache.count(request.target_id) == 0)
            {
                auto &item = segment_cache[request.target_id];
                item.desc = getSegmentDescByID(request.target_id);
                for (auto &context : context_list_)
                {
                    auto endpoint = context->endpoint(MakeNicPath(item.desc->name, context->deviceName()));
                    if (!endpoint->connected())
                        endpoint->setupConnectionsByActive();
                    item.endpoints.push_back(endpoint);
                }
            }
        }

        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            ++task_id;
            auto &cache = segment_cache[request.target_id];
            const static size_t kBlockSize = 65536;
            for (uint64_t offset = 0; offset < request.length; offset += kBlockSize)
            {
                int rnic_index = rnic_prob_list_[lrand48() % 64];
                auto context = context_list_[rnic_index];
                auto slice = new Slice();
                slice->source_addr = (char *)request.source + offset;
                slice->length = std::min(request.length - offset, kBlockSize);
                slice->opcode = request.opcode;
                slice->rdma.dest_addr = request.target_offset + offset;
                for (auto &item : cache.desc->buffers)
                {
                    if (slice->rdma.dest_addr >= item.addr && slice->rdma.dest_addr < item.addr + item.length)
                    {
                        slice->rdma.dest_rkey = item.rkey[rnic_index];
                        break;
                    }
                }
                slice->rdma.source_lkey = context->lkey(slice->source_addr);
                slice->task = &task;
                slice->status.store(Slice::PENDING, std::memory_order_relaxed);
                task.total_bytes += slice->length;
                task.slices.push_back(slice);
                slices_to_post[cache.endpoints[rnic_index]].push_back(slice);
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
        const size_t task_count = batch_desc_ptr->task_list.size();
        for (size_t task_id = 0; task_id < task_count; task_id++)
        {
            auto &task = batch_desc_ptr->task_list[task_id];
            if (task.success_slice_count + task.failed_slice_count < (uint64_t)task.slices.size())
            {
                LOG(ERROR) << "BatchID cannot be freed until all tasks are done";
                return -1;
            }
        }
        batch_desc_set_.erase(batch_desc_ptr);
        delete batch_desc_ptr;
        return 0;
    }

    int TransferEngine::onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        auto local_nic_name = getNicNameFromNicPath(peer_desc.peer_nic_path);
        for (auto &context : context_list_)
        {
            if (context->deviceName() == local_nic_name)
            {
                auto endpoint = context->endpoint(peer_desc.local_nic_path);
                endpoint->setupConnectionsByPassive(peer_desc, local_desc);
                break;
            }
        }
        return 0;
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
            auto context = std::make_shared<RdmaContext>(this);
            if (context->construct(rnic))
                return -1;
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

    int TransferEngine::updateRnicLinkSpeed(const std::vector<int> &rnic_speed)
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
