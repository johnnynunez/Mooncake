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
        int ret = metadata_->parseNicPriorityMatrix(nic_priority_matrix, local_priority_matrix_, device_name_list_);
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

    int TransferEngine::registerLocalMemory(void *addr, size_t length, const std::string &name)
    {
        BufferDesc desc;
        desc.name = name;
        desc.addr = (uint64_t)addr;
        desc.length = length;
        const static int access_rights = IBV_ACCESS_LOCAL_WRITE |
                                         IBV_ACCESS_REMOTE_WRITE |
                                         IBV_ACCESS_REMOTE_READ;
        for (auto &context : context_list_)
        {
            int ret = context->registerMemoryRegion(addr, length, access_rights);
            if (ret)
                return -1;
            desc.rkey.push_back(context->rkey(addr));
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
                if (iter->addr == (uint64_t)addr)
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
        auto batch_desc = std::make_shared<BatchDesc>();
        batch_desc->id = BatchID(batch_desc.get());
        batch_desc->batch_size = batch_size;
        batch_desc->task_list.reserve(batch_size);
        batch_desc_lock_.WLock();
        batch_desc_set_[batch_desc->id] = batch_desc;
        batch_desc_lock_.WUnlock();
        return batch_desc->id;
    }

    int TransferEngine::submitTransfer(BatchID batch_id,
                                       const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        std::unordered_map<std::shared_ptr<RdmaEndPoint>, std::vector<Slice *>> slices_to_post;
        size_t task_id = batch_desc.task_list.size();
        batch_desc.task_list.resize(task_id + entries.size());

        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            ++task_id;
            const static size_t kBlockSize = 65536;
            for (uint64_t offset = 0; offset < request.length; offset += kBlockSize)
            {
                auto slice = std::make_shared<Slice>();
                slice->source_addr = (char *)request.source + offset;
                slice->length = std::min(request.length - offset, kBlockSize);
                slice->opcode = request.opcode;
                slice->rdma.dest_addr = request.target_offset + offset;
                auto context = selectLocalContext(slice->source_addr, slice->rdma.source_lkey);
                slice->task = &task;
                slice->status.store(Slice::PENDING, std::memory_order_relaxed);
                std::string peer_nic_path;
                selectPeerContext(request.target_id, request.target_offset, peer_nic_path, slice->rdma.dest_rkey);
                auto endpoint = context->endpoint(peer_nic_path);
                if (!endpoint->connected())
                    endpoint->setupConnectionsByActive(peer_nic_path);
                slices_to_post[endpoint].push_back(slice.get());
                task.slices.push_back(slice);
                task.total_bytes += slice->length;
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
        auto &batch_desc = *((BatchDesc *)(batch_id));
        const size_t task_count = batch_desc.task_list.size();
        for (size_t task_id = 0; task_id < task_count; task_id++)
        {
            auto &task = batch_desc.task_list[task_id];
            if (task.success_slice_count + task.failed_slice_count < (uint64_t)task.slices.size())
            {
                LOG(ERROR) << "BatchID cannot be freed until all tasks are done";
                return -1;
            }
        }
        batch_desc_set_.erase(batch_id);
        return 0;
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
            desc.buffers = registered_buffer_list_;
        }

        desc.priority_matrix = local_priority_matrix_;
        return metadata_->updateSegmentDesc(local_server_name_, desc);
    }

    int TransferEngine::removeLocalSegmentDesc()
    {
        return metadata_->removeSegmentDesc(local_server_name_);
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
        if (device_name_list_.empty())
        {
            LOG(ERROR) << "No available RNIC!";
            return -1;
        }

        std::vector<int> device_speed_list;
        for (auto &device_name : device_name_list_)
        {
            auto context = std::make_shared<RdmaContext>(*this, device_name);
            if (context->construct())
                return -1;
            device_speed_list.push_back(context->activeSpeed());
            context_list_.push_back(context);
        }

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

    RdmaContext *TransferEngine::selectLocalContext(void *source_addr, uint32_t &lkey)
    {
        RWSpinlock::ReadGuard guard(registered_buffer_lock_);
        for (auto &item : registered_buffer_list_)
            if ((char *)item.addr <= (char *)source_addr && (char *)source_addr < (char *)item.addr + item.length)
            {
                if (!local_priority_matrix_.count(item.name))
                    continue;
                auto &priority = local_priority_matrix_[item.name];
                std::string device_name;
                if (!priority.preferred_rnic_list.empty())
                    device_name = priority.preferred_rnic_list[lrand48() % priority.preferred_rnic_list.size()];
                if (!priority.available_rnic_list.empty())
                    device_name = priority.available_rnic_list[lrand48() % priority.available_rnic_list.size()];

                int device_index = 0;
                for (auto context : context_list_)
                {
                    if (context->deviceName() == device_name)
                    {
                        lkey = context->lkey(source_addr);
                        return context_list_[device_index].get();
                    }
                    device_index++;
                }
            }
        return nullptr;
    }

    int TransferEngine::selectPeerContext(uint64_t target_id, uint64_t target_offset, std::string &peer_device_name, uint32_t &dest_rkey)
    {
        auto desc = getSegmentDescByID(target_id);
        for (auto &item : desc->buffers)
            if (item.addr <= target_offset && target_offset < item.addr + item.length)
            {
                auto &priority = desc->priority_matrix[item.name];
                std::string device_name;
                if (!priority.preferred_rnic_list.empty())
                    device_name = priority.preferred_rnic_list[lrand48() % priority.preferred_rnic_list.size()];
                if (!priority.available_rnic_list.empty())
                    device_name = priority.available_rnic_list[lrand48() % priority.available_rnic_list.size()];
                peer_device_name = MakeNicPath(desc->name, device_name);
                int device_index = 0;
                for (auto context : desc->devices)
                {
                    if (context.name == device_name)
                    {
                        dest_rkey = item.rkey[device_index];
                        return 0;
                    }
                    device_index++;
                }
            }
        return -1;
    }
}
