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
          next_segment_id_(1),
          local_server_name_(local_server_name)
    {
        TransferMetadata::PriorityMatrix local_priority_matrix;

        int ret = metadata_->parseNicPriorityMatrix(nic_priority_matrix, local_priority_matrix, device_name_list_);
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot parse nic priority matrix";
            exit(EXIT_FAILURE);
        }

        int device_index = 0;
        for (auto &device_name : device_name_list_)
            device_name_to_index_map_[device_name] = device_index++;

        ret = initializeRdmaResources();
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot initialize rdma resources";
            exit(EXIT_FAILURE);
        }

        ret = allocateLocalSegmentID(local_priority_matrix);
        if (ret)
        {
            LOG(ERROR) << "Transfer engine cannot be initialized: cannot allocate local segment";
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
        BufferDesc buffer_desc;
        buffer_desc.name = name;
        buffer_desc.addr = (uint64_t)addr;
        buffer_desc.length = length;
        const static int access_rights = IBV_ACCESS_LOCAL_WRITE |
                                         IBV_ACCESS_REMOTE_WRITE |
                                         IBV_ACCESS_REMOTE_READ;
        for (auto &context : context_list_)
        {
            int ret = context->registerMemoryRegion(addr, length, access_rights);
            if (ret)
                return -1;
            buffer_desc.lkey.push_back(context->lkey(addr));
            buffer_desc.rkey.push_back(context->rkey(addr));
        }
        {
            RWSpinlock::WriteGuard guard(segment_lock_);
            auto new_segment_desc = std::make_shared<SegmentDesc>();
            if (!new_segment_desc)
                return -1;
            auto &segment_desc = segment_id_to_desc_map_[LOCAL_SEGMENT_ID];
            *new_segment_desc = *segment_desc;
            segment_desc = new_segment_desc;
            segment_desc->buffers.push_back(buffer_desc);
        }
        return updateLocalSegmentDesc();
    }

    int TransferEngine::unregisterLocalMemory(void *addr)
    {
        {
            RWSpinlock::WriteGuard guard(segment_lock_);
            auto new_segment_desc = std::make_shared<SegmentDesc>();
            if (!new_segment_desc)
                return -1;
            auto &segment_desc = segment_id_to_desc_map_[LOCAL_SEGMENT_ID];
            *new_segment_desc = *segment_desc;
            segment_desc = new_segment_desc;
            for (auto iter = segment_desc->buffers.begin();
                 iter != segment_desc->buffers.end();
                 ++iter)
            {
                if (iter->addr == (uint64_t)addr)
                {
                    for (auto &entry : context_list_)
                        entry->unregisterMemoryRegion(addr);
                    segment_desc->buffers.erase(iter);
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
        batch_desc_lock_.lock();
        batch_desc_set_[batch_desc->id] = batch_desc;
        batch_desc_lock_.unlock();
        return batch_desc->id;
    }

    int TransferEngine::submitTransfer(BatchID batch_id,
                                       const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
            return -1;

        std::unordered_map<std::shared_ptr<RdmaContext>, std::vector<Slice *>> slices_to_post;
        size_t task_id = batch_desc.task_list.size();
        batch_desc.task_list.resize(task_id + entries.size());

        std::unordered_map<SegmentID, std::shared_ptr<SegmentDesc>> segment_desc_map;
        segment_desc_map[LOCAL_SEGMENT_ID] = getSegmentDescByID(LOCAL_SEGMENT_ID);
        for (auto &request : entries)
        {
            auto target_id = request.target_id;
            if (!segment_desc_map.count(target_id))
                segment_desc_map[target_id] = getSegmentDescByID(target_id);
        }

        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            ++task_id;
            const static size_t kBlockSize = 65536;
            for (uint64_t offset = 0; offset < request.length; offset += kBlockSize)
            {
                auto slice = std::make_unique<Slice>();
                slice->source_addr = (char *)request.source + offset;
                slice->length = std::min(request.length - offset, kBlockSize);
                slice->opcode = request.opcode;
                slice->rdma.dest_addr = request.target_offset + offset;

                auto &local_segment_desc = segment_desc_map[LOCAL_SEGMENT_ID];
                std::string device_name;
                int buffer_id, device_id;

                selectDevice(local_segment_desc, (uint64_t)slice->source_addr, buffer_id, device_id);
                auto &context = context_list_[device_id];
                slice->rdma.source_lkey = local_segment_desc->buffers[buffer_id].lkey[device_id];
                slice->rdma.retry_cnt = 0;
                slice->rdma.max_retry_cnt = 4;
                slice->task = &task;
                slice->status = Slice::PENDING;
                slice->peer_segment_desc = segment_desc_map[request.target_id];
                slices_to_post[context].push_back(slice.get());
                task.total_bytes += slice->length;
                task.slices.push_back(std::move(slice));
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
                task.is_finished = true;
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
            if (!batch_desc.task_list[task_id].is_finished)
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

    int TransferEngine::allocateLocalSegmentID(TransferMetadata::PriorityMatrix &priority_matrix)
    {
        RWSpinlock::WriteGuard guard(segment_lock_);
        auto desc = std::make_shared<SegmentDesc>();
        if (!desc)
            return -1;
        desc->name = local_server_name_;
        for (auto &entry : context_list_)
        {
            TransferMetadata::DeviceDesc device_desc;
            device_desc.name = entry->deviceName();
            device_desc.lid = entry->lid();
            device_desc.gid = entry->gid();
            desc->devices.push_back(device_desc);
        }
        desc->priority_matrix = priority_matrix;
        segment_id_to_desc_map_[LOCAL_SEGMENT_ID] = desc;
        segment_name_to_id_map_[local_server_name_] = LOCAL_SEGMENT_ID;
        return 0;
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
        RWSpinlock::ReadGuard guard(segment_lock_);
        auto desc = segment_id_to_desc_map_[LOCAL_SEGMENT_ID];
        return metadata_->updateSegmentDesc(local_server_name_, *desc);
    }

    int TransferEngine::removeLocalSegmentDesc()
    {
        return metadata_->removeSegmentDesc(local_server_name_);
    }

    int TransferEngine::onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        auto local_nic_name = getNicNameFromNicPath(peer_desc.peer_nic_path);
        auto context = context_list_[device_name_to_index_map_[local_nic_name]];
        auto endpoint = context->endpoint(peer_desc.local_nic_path);
        endpoint->setupConnectionsByPassive(peer_desc, local_desc);
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

    int TransferEngine::selectDevice(std::shared_ptr<SegmentDesc> &desc, uint64_t offset, int &buffer_id, int &device_id)
    {
        for (buffer_id = 0; buffer_id < (int)desc->buffers.size(); ++buffer_id)
        {
            auto &buffer_desc = desc->buffers[buffer_id];
            if (buffer_desc.addr > offset || offset >= buffer_desc.addr + buffer_desc.length)
                continue;

            auto &priority = desc->priority_matrix[buffer_desc.name];
            std::string device_name;
            if (!priority.preferred_rnic_list.empty())
                device_name = priority.preferred_rnic_list[lrand48() % priority.preferred_rnic_list.size()];
            if (device_name.empty() && !priority.available_rnic_list.empty())
                device_name = priority.available_rnic_list[lrand48() % priority.available_rnic_list.size()];
            if (device_name.empty())
                return -1;

            for (device_id = 0; device_id < (int)desc->devices.size(); ++device_id)
                if (desc->devices[device_id].name == device_name)
                    return 0;
            return -1;
        }

        return -1;
    }
}
