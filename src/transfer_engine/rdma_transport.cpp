// transfer_engine.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/rdma_transport.h"
#include "transfer_engine/common.h"
#include "transfer_engine/config.h"
#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"

#include <cassert>
#include <cstddef>
#include <future>
#include <glog/logging.h>
#include <set>
#include <sys/mman.h>
#include <sys/time.h>

namespace mooncake
{
    RdmaTransport::RdmaTransport()
        : next_segment_id_(1) {}

    RdmaTransport::~RdmaTransport()
    {
#ifdef CONFIG_USE_BATCH_DESC_SET
        for (auto &entry : batch_desc_set_)
            delete entry.second;
        batch_desc_set_.clear();
#endif
        metadata_->removeSegmentDesc(local_server_name_);
        batch_desc_set_.clear();
        context_list_.clear();
    }

    int RdmaTransport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args) {
        const std::string nic_priority_matrix = static_cast<char *>(args[0]);
        bool dry_run = args[1] ? *static_cast<bool *>(args[1]) : false;
        TransferMetadata::PriorityMatrix local_priority_matrix;

        if (dry_run)
            return 0;

        metadata_ = meta;
        local_server_name_ = local_server_name;
        
        int ret = metadata_->parseNicPriorityMatrix(nic_priority_matrix, local_priority_matrix, device_name_list_);
        if (ret)
        {
            LOG(ERROR) << "*** Transfer engine cannot be initialized: cannot parse NIC priority matrix";
            LOG(ERROR) << "*** nic_priority_matrix " << nic_priority_matrix;
            return -1;
        }

        int device_index = 0;
        for (auto &device_name : device_name_list_)
            device_name_to_index_map_[device_name] = device_index++;

        ret = initializeRdmaResources();
        if (ret)
        {
            LOG(ERROR) << "*** Transfer engine cannot be initialized: cannot initialize RDMA resources";
            return -1;
        }

        ret = allocateLocalSegmentID(local_priority_matrix);
        if (ret)
        {
            LOG(ERROR) << "*** Transfer engine cannot be initialized: cannot allocate local segment";
            return -1;
        }

        ret = startHandshakeDaemon();
        if (ret)
        {
            LOG(ERROR) << "*** Transfer engine cannot be initialized: cannot start handshake daemon";
            LOG(ERROR) << "*** Try to set environment variable MC_HANDSHAKE_PORT to another value";
            return -1;
        }

        ret = metadata_->updateLocalSegmentDesc();
        if (ret)
        {
            LOG(ERROR) << "*** Transfer engine cannot be initialized: cannot publish segments";
            LOG(ERROR) << "*** Check the connectivity between this server and metadata server (etcd/memcached)";
            return -1;
        }

        return 0;
    }

    int RdmaTransport::registerLocalMemory(void *addr, size_t length, const std::string &name, bool update_metadata)
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
                return ret;
            buffer_desc.lkey.push_back(context->lkey(addr));
            buffer_desc.rkey.push_back(context->rkey(addr));
        }
        int rc = metadata_->addLocalMemoryBuffer(buffer_desc, update_metadata);

        if (rc)
            return rc;

        return 0;
    }

    int RdmaTransport::unregisterLocalMemory(void *addr, bool update_metadata)
    {
        int rc = metadata_->removeLocalMemoryBuffer(addr, update_metadata);
        if (rc)
            return rc;

        for (auto &context : context_list_)
            context->unregisterMemoryRegion(addr);

        return 0;
    }

    int RdmaTransport::allocateLocalSegmentID(TransferMetadata::PriorityMatrix &priority_matrix)
    {
        auto desc = std::make_shared<SegmentDesc>();
        if (!desc)
            return ERR_MEMORY;
        desc->name = local_server_name_;
        desc->protocol = "rdma";
        for (auto &entry : context_list_)
        {
            TransferMetadata::DeviceDesc device_desc;
            device_desc.name = entry->deviceName();
            device_desc.lid = entry->lid();
            device_desc.gid = entry->gid();
            desc->devices.push_back(device_desc);
        }
        desc->priority_matrix = priority_matrix;
        metadata_->addLocalSegment(LOCAL_SEGMENT_ID, local_server_name_, std::move(desc));
        return 0;
    }

    int RdmaTransport::registerLocalMemoryBatch(const std::vector<RdmaTransport::BufferEntry> &buffer_list,
                                                 const std::string &location)
    {
        std::vector<std::future<int>> results;
        for (auto &buffer : buffer_list)
        {
            results.emplace_back(std::async(std::launch::async, [this, buffer, location]() -> int
                                            { return registerLocalMemory(buffer.addr, buffer.length, location, false); }));
        }

        for (size_t i = 0; i < buffer_list.size(); ++i)
        {
            if (results[i].get())
            {
                LOG(WARNING) << "Failed to register memory: addr " << buffer_list[i].addr
                             << " length " << buffer_list[i].length;
            }
        }

        return metadata_->updateLocalSegmentDesc();
    }

    int RdmaTransport::unregisterLocalMemoryBatch(const std::vector<void *> &addr_list)
    {
        std::vector<std::future<int>> results;
        for (auto &addr : addr_list)
        {
            results.emplace_back(std::async(std::launch::async, [this, addr]() -> int
                                            { return unregisterLocalMemory(addr, false); }));
        }

        for (size_t i = 0; i < addr_list.size(); ++i)
        {
            if (results[i].get())
                LOG(WARNING) << "Failed to unregister memory: addr " << addr_list[i];
        }

        return metadata_->updateLocalSegmentDesc();
    }

    int RdmaTransport::submitTransfer(
        BatchID batch_id, const std::vector<TransferRequest> &entries)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        if (batch_desc.task_list.size() + entries.size() > batch_desc.batch_size)
        {
            LOG(ERROR) << "Exceed the limitation of current batch's capacity";
            return ERR_TOO_MANY_REQUESTS;
        }

        std::unordered_map<std::shared_ptr<RdmaContext>, std::vector<Slice *>> slices_to_post;
        size_t task_id = batch_desc.task_list.size();
        batch_desc.task_list.resize(task_id + entries.size());
        auto local_segment_desc = metadata_->getSegmentDescByID(LOCAL_SEGMENT_ID);
        const size_t kBlockSize = globalConfig().slice_size;
        const int kMaxRetryCount = globalConfig().retry_cnt;

        for (auto &request : entries)
        {
            TransferTask &task = batch_desc.task_list[task_id];
            ++task_id;
            for (uint64_t offset = 0; offset < request.length; offset += kBlockSize)
            {
                auto slice = new Slice();
                slice->source_addr = (char *)request.source + offset;
                slice->length = std::min(request.length - offset, kBlockSize);
                slice->opcode = request.opcode;
                slice->rdma.dest_addr = request.target_offset + offset;
                slice->rdma.retry_cnt = 0;
                slice->rdma.max_retry_cnt = kMaxRetryCount;
                slice->task = &task;
                slice->target_id = request.target_id;
                slice->status = Slice::PENDING;

                int buffer_id = -1, device_id = -1, retry_cnt = 0;
                while (retry_cnt < kMaxRetryCount)
                {
                    if (selectDevice(local_segment_desc.get(), (uint64_t)slice->source_addr, slice->length, buffer_id, device_id, retry_cnt++))
                        continue;
                    auto &context = context_list_[device_id];
                    if (!context->active())
                        continue;
                    slice->rdma.source_lkey = local_segment_desc->buffers[buffer_id].lkey[device_id];
                    slices_to_post[context].push_back(slice);
                    task.total_bytes += slice->length;
                    task.slices.push_back(slice);
                    break;
                }
                if (device_id < 0)
                {
                    LOG(ERROR) << "Address not registered by any device(s) " << slice->source_addr;
                    return ERR_ADDRESS_NOT_REGISTERED;
                }
            }
        }
        for (auto &entry : slices_to_post)
            entry.first->submitPostSend(entry.second);
        return 0;
    }

    int RdmaTransport::getTransferStatus(BatchID batch_id,
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
            if (success_slice_count + failed_slice_count ==
                (uint64_t)task.slices.size())
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

    int RdmaTransport::getTransferStatus(BatchID batch_id, size_t task_id,
                                          TransferStatus &status)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        const size_t task_count = batch_desc.task_list.size();
        if (task_id >= task_count)
            return ERR_INVALID_ARGUMENT;
        auto &task = batch_desc.task_list[task_id];
        status.transferred_bytes = task.transferred_bytes;
        uint64_t success_slice_count = task.success_slice_count;
        uint64_t failed_slice_count = task.failed_slice_count;
        if (success_slice_count + failed_slice_count ==
            (uint64_t)task.slices.size())
        {
            if (failed_slice_count)
                status.s = TransferStatusEnum::FAILED;
            else
                status.s = TransferStatusEnum::COMPLETED;
            task.is_finished = true;
        }
        else
        {
            status.s = TransferStatusEnum::WAITING;
        }
        return 0;
    }

    RdmaTransport::SegmentID RdmaTransport::getSegmentID(const std::string &segment_name)
    {
        return metadata_->getSegmentID(segment_name);
    }

    int RdmaTransport::onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        auto local_nic_name = getNicNameFromNicPath(peer_desc.peer_nic_path);
        if (local_nic_name.empty())
            return ERR_INVALID_ARGUMENT;
        auto context = context_list_[device_name_to_index_map_[local_nic_name]];
        auto endpoint = context->endpoint(peer_desc.local_nic_path);
        if (!endpoint)
            return ERR_ENDPOINT;
        return endpoint->setupConnectionsByPassive(peer_desc, local_desc);
    }

    int RdmaTransport::initializeRdmaResources()
    {
        if (device_name_list_.empty())
        {
            LOG(ERROR) << "No available RNIC!";
            return ERR_DEVICE_NOT_FOUND;
        }

        std::vector<int> device_speed_list;
        for (auto &device_name : device_name_list_)
        {
            auto context = std::make_shared<RdmaContext>(*this, device_name);
            if (!context)
                return ERR_MEMORY;

            auto &config = globalConfig();
            int ret = context->construct(config.num_cq_per_ctx,
                                         config.num_comp_channels_per_ctx,
                                         config.port,
                                         config.gid_index,
                                         config.max_cqe,
                                         config.max_ep_per_ctx);
            if (ret)
                return ret;
            device_speed_list.push_back(context->activeSpeed());
            context_list_.push_back(context);
        }

        return 0;
    }

    int RdmaTransport::startHandshakeDaemon()
    {
        return metadata_->startHandshakeDaemon(
            std::bind(&RdmaTransport::onSetupRdmaConnections, this,
                      std::placeholders::_1, std::placeholders::_2),
            globalConfig().handshake_port);
    }

    int RdmaTransport::selectDevice(SegmentDesc *desc, uint64_t offset, size_t length, int &buffer_id, int &device_id, int retry_count)
    {
        for (buffer_id = 0; buffer_id < (int)desc->buffers.size(); ++buffer_id)
        {
            auto &buffer_desc = desc->buffers[buffer_id];
            if (buffer_desc.addr > offset || offset + length > buffer_desc.addr + buffer_desc.length)
                continue;

            auto &priority = desc->priority_matrix[buffer_desc.name];
            size_t preferred_rnic_list_len = priority.preferred_rnic_list.size();
            size_t available_rnic_list_len = priority.available_rnic_list.size();
            size_t rnic_list_len = preferred_rnic_list_len + available_rnic_list_len;
            if (rnic_list_len == 0)
                return ERR_DEVICE_NOT_FOUND;

            if (retry_count == 0)
            {
                int rand_value = SimpleRandom::Get().next();
                if (preferred_rnic_list_len)
                    device_id = priority.preferred_rnic_id_list[rand_value % preferred_rnic_list_len];
                else
                    device_id = priority.available_rnic_id_list[rand_value % available_rnic_list_len];
            }
            else
            {
                size_t index = (retry_count - 1) % rnic_list_len;
                if (index < preferred_rnic_list_len)
                    device_id = index;
                else
                    device_id = index - preferred_rnic_list_len;
            }

            return 0;
        }

        return ERR_ADDRESS_NOT_REGISTERED;
    }
}
