#include <string.h>
#include "cache_allocator.h"

namespace mooncake {

CacheAllocator::CacheAllocator(size_t shard_size, std::unique_ptr<AllocationStrategy> strategy)
    : shard_size_(shard_size), allocation_strategy_(std::move(strategy)), global_version_(0) {}

ReplicaList CacheAllocator::allocateReplicas(size_t obj_size, int num_replicas) {
    std::cout << "Allocating replicas for object size: " << obj_size << ", num replicas: " << num_replicas << std::endl;

    int num_shards = (obj_size + shard_size_ - 1) / shard_size_;
    SelectNodesType selected_nodes = allocation_strategy_->selectNodes(num_shards, num_replicas, shard_size_, buf_allocators_);

    ReplicaList replicas(num_replicas);
    size_t remaining_size = obj_size;
    int current_replica = 0;
    int shard_index = 0;
    
    for (const auto& node : selected_nodes) {
        size_t shard_size = std::min(remaining_size, node.shard_size);
        auto& allocator = buf_allocators_[node.category][node.segment_id][node.allocator_index];
        BufHandle handle = allocator.allocate(shard_size);
        
        if (handle.status != BufStatus::INIT) {
            throw std::runtime_error("Failed to allocate buffer");
        }
        
        replicas[current_replica].handles.push_back(handle);
        
        std::cout << "  Replica " << current_replica + 1 << ", Shard " << shard_index + 1 
                  << ": Category " << node.category << ", Segment " << node.segment_id 
                  << ", Allocator " << node.allocator_index 
                  << ", Offset " << handle.offset << ", Size " << handle.size << std::endl;
        
        remaining_size -= shard_size;
        shard_index++;
        
        if (remaining_size == 0 || shard_index == num_shards) {
            replicas[current_replica].status = ReplicaStatus::INITIALIZED;
            current_replica++;
            if (current_replica < num_replicas) {
                remaining_size = obj_size;  // 为下一个副本重置剩余大小
                shard_index = 0;
            } else {
                break;  // 所有副本都已分配完成
            }
        }
    }

    if (current_replica < num_replicas) {
        throw std::runtime_error("Failed to allocate all requested replicas");
    }
    return replicas;
}


void CacheAllocator::generateWriteTransferRequests(ReplicaList &replicas, const std::vector<void *> &ptrs, const std::vector<void *> &sizes, int num_replicas, std::vector<TransferRequest>& transfer_requests) {

    for (int replica_idx = 0; replica_idx < num_replicas; ++replica_idx) {
        size_t written = 0;
        size_t input_offset = 0;
        size_t input_idx = 0;

        // shard类型为 BufHandle
        for (const auto &shard : replicas[replica_idx].handles) {

            size_t shard_offset = 0;

            while (shard_offset < shard.size && input_idx < ptrs.size()) {
                size_t input_size = reinterpret_cast<size_t>(sizes[input_idx]);
                size_t remaining_input = input_size - input_offset;
                size_t remaining_shard = shard.size - shard_offset;
                size_t to_write = std::min(remaining_input, remaining_shard);

                // Copy data
                TransferRequest request;
                request.opcode = TransferRequest::OpCode::WRITE;
                request.source = (void*)(static_cast<char *>(ptrs[input_idx]) + input_offset);
                request.length = to_write;
                request.target_id = shard.segment_id;
                request.target_offset = shard.offset + shard_offset;
                transfer_requests.push_back(std::move(request));


                shard_offset += to_write;
                input_offset += to_write;
                written += to_write;

                if (input_offset == input_size) {
                    input_idx++;
                    input_offset = 0;
                }
            }

            std::cout << "Written " << shard_offset << " bytes to shard in node " << shard.segment_id << std::endl;
        }

        for (auto &shard : replicas[replica_idx].handles) {
            // Update status
            shard.status = BufStatus::COMPLETE;
        }
        replicas[replica_idx].status = ReplicaStatus::COMPLETED;
        std::cout << "Total written for replica " << replica_idx << ": " << written << " bytes" << std::endl;
    }
    return;
}

void CacheAllocator::updateObjectMeta(const ObjectKey &key, const ReplicaList &replicas, const ReplicateConfig &config) {
    Version new_version = ++global_version_;
    auto &version_list = object_meta_[key];
    version_list.versions[new_version] = replicas;
    version_list.flushed_version = new_version;
    version_list.config = config;

    std::cout << "Updated object meta for key: " << key << ", new version: " << new_version 
              << ", num replicas: " << config.num_replicas << std::endl;
}

std::pair<Version, ReplicaList> CacheAllocator::getReplicas(const ObjectKey &key, Version min_version) {
    std::cout << "Getting replicas for key: " << key << ", minimum version: " << min_version << std::endl;

    auto it = object_meta_.find(key);
    if (it == object_meta_.end()) {
        std::cout << "Object not found: " << key << std::endl;
        throw std::runtime_error("Object not found");
    }

    const auto &version_list = it->second;

    // Find the latest version greater than or equal to min_version
    auto version_it = version_list.versions.lower_bound(min_version);
    if (version_it == version_list.versions.end()) {
        std::cout << "No version found greater than or equal to: " << min_version << std::endl;
        throw std::runtime_error("No suitable version found");
    }

    Version selected_version = version_it->first;
    std::cout << "Found replicas for version: " << selected_version << std::endl;
    return std::make_pair(version_it->first, version_it->second);
}

void CacheAllocator::generateReadTransferRequests(const std::vector<BufHandle> &replica, 
                                       size_t offset, std::vector<void *> &ptrs, 
                                       const std::vector<void *> &sizes,std::vector<TransferRequest>& transfer_requests) {
    
    size_t total_size = 0;
    for (const auto &size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }

    size_t current_offset = 0;
    size_t remaining_offset = offset; // offset in input
    size_t bytes_read = 0;
    size_t output_index = 0; // ptrs index
    size_t output_offset = 0; // offset in one ptr

    for (const auto &shard : replica) {
        if (current_offset + shard.size <= offset) {
            current_offset += shard.size;
            continue;
        }

        size_t shard_start = (remaining_offset > shard.size) ? 0 : remaining_offset;
        remaining_offset = (remaining_offset > shard.size) ? remaining_offset - shard.size : 0;

       
        TransferRequest request;

        while (shard_start < shard.size && bytes_read < total_size) {
            size_t bytes_to_read = std::min({
                shard.size - shard_start,
                reinterpret_cast<size_t>(sizes[output_index]) - output_offset,
                total_size - bytes_read
            });
                
            request.source = (void*)(static_cast<char *>(ptrs[output_index]) + output_offset);
            request.target_id = shard.segment_id;
            request.target_offset = shard.offset;
            request.length = bytes_to_read;
            request.opcode = TransferRequest::OpCode::READ;
            transfer_requests.push_back(std::move(request));

            shard_start += bytes_to_read;
            output_offset += bytes_to_read;
            bytes_read += bytes_to_read;

            if (output_offset == reinterpret_cast<size_t>(sizes[output_index])) {
                output_index++;
                output_offset = 0;
            }
        }

        current_offset += shard.size;
    }

    return;
                                        
}

TaskID CacheAllocator::makePut(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, ReplicateConfig config, std::vector<TransferRequest>& transfer_requests) {
    if (ptrs.size() != sizes.size()) {
        throw std::invalid_argument("ptrs and sizes vectors must have the same length");
    }

    size_t total_size = 0;
    for (const auto& size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }
    std::cout << "AsyncPut: key=" << key << ", total_size=" << total_size << ", num_replicas=" << config.num_replicas << std::endl;
    
    ReplicaList replicas = allocateReplicas(total_size, config.num_replicas);
    generateWriteTransferRequests(replicas, ptrs, sizes, config.num_replicas, transfer_requests);
    updateObjectMeta(key, replicas, config);
    
    std::cout << "AsyncPut completed, TaskID: " << global_version_.load() << std::endl;
    return global_version_.load();
}

TaskID CacheAllocator::makeReplicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff, std::vector<TransferRequest>& transfer_tasks) {
    std::cout << "AsyncReplicate: key=" << key << ", new_num_replicas=" << new_config.num_replicas << std::endl;
    
    auto it = object_meta_.find(key);
    if (it == object_meta_.end()) {
        std::cout << "Object not found: " << key << std::endl;
        throw std::runtime_error("Object not found");
    }
    
    VersionList& version_list = it->second;
    ReplicateConfig old_config = version_list.config;
    ReplicaList current_replicas = version_list.versions[version_list.flushed_version];
    
    std::cout << "Current num_replicas: " << old_config.num_replicas << std::endl;
    
    if (new_config.num_replicas > old_config.num_replicas) {
        std::cout << "Adding " << new_config.num_replicas - old_config.num_replicas << " new replicas" << std::endl;
        size_t obj_size = 0;
        for (const auto& handle : current_replicas[0].handles) {
            obj_size += handle.size;
        }

        ReplicaList new_replicas = allocateReplicas(obj_size, new_config.num_replicas - old_config.num_replicas);

        // Transfer data from old replicas to new replicas
        for (auto& replica : new_replicas) {
            int index = 0;
            for (const auto& handle : current_replicas[0].handles) {
                TransferRequest request;
                request.source_replica.target_id = handle.segment_id;
                request.source_replica.target_offset = handle.offset;
                request.source_replica.length = handle.size;
                request.opcode = TransferRequest::OpCode::REPLICA_INCR;
                request.target_id = replica.handles[index].segment_id;
                request.target_offset = replica.handles[index].offset;
                request.length = replica.handles[index].size;
                index++;
                transfer_tasks.push_back(request);
            }
            replica.status = ReplicaStatus::INITIALIZED;
        }

        replica_diff.added_replicas = new_replicas;
        current_replicas.insert(current_replicas.end(), new_replicas.begin(), new_replicas.end());
        replica_diff.change_status = ReplicaChangeStatus::ADDED;
    } else if (new_config.num_replicas < old_config.num_replicas) {
        // TODO : 这里删除时 暂时没有考虑副本有刚刚创建的情况，假设副本都是可用状态
        std::cout << "Removing " << old_config.num_replicas - new_config.num_replicas << " replicas" << std::endl;
        replica_diff.removed_replicas.assign(
            current_replicas.begin() + new_config.num_replicas,
            current_replicas.end()
        );

        for (int i = new_config.num_replicas; i < old_config.num_replicas; ++i) {
            int index = 0;
            for (const auto& handle : current_replicas[i].handles) {
                // virtual_nodes_[handle.segment_id]->deallocate(handle);
                TransferRequest request;
                request.source_replica.target_id = handle.segment_id;
                request.source_replica.target_offset = handle.offset;
                request.source_replica.length = handle.size;
                request.opcode = TransferRequest::OpCode::REPLICA_DECR;
                transfer_tasks.push_back(std::move(request));
                std::cout << "Deallocated: Node " << handle.segment_id << ", Offset " << handle.offset << ", Size " << handle.size << std::endl;
            }
        }
        current_replicas.resize(new_config.num_replicas);
        replica_diff.change_status = ReplicaChangeStatus::REMOVED;
    } else {
        replica_diff.change_status = ReplicaChangeStatus::NO_CHANGE;
    }
    
    updateObjectMeta(key, current_replicas, new_config);
    
    std::cout << "AsyncReplicate completed, TaskID: " << global_version_.load() << std::endl;
    return global_version_.load();
}

TaskID CacheAllocator::makeGet(ObjectKey key, PtrType type, std::vector<void *> ptrs, std::vector<void *> sizes, Version min_version, size_t offset, std::vector<TransferRequest>& transfer_tasks) {
    std::cout << "AsyncGet: key=" << key << ", min_version=" << min_version << ", offset=" << offset << std::endl;

    const std::pair<Version, ReplicaList>& replicas = getReplicas(key, min_version);

    size_t total_size = 0;
    for (const auto& size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }

    std::cout << "Total size to read: " << total_size << " bytes" << std::endl;

    // 选择从第一个副本进行读取（可以根据需要实现更复杂的选择逻辑）
   
    for (const auto& replica : replicas.second) {
        if (replica.status == ReplicaStatus::COMPLETED) {
            std::cout << "Reading from replica with status COMPLETED" << std::endl;
            generateReadTransferRequests(replica.handles, offset, ptrs, sizes, transfer_tasks);
            break;
        }
    }
    std::cout << "AsyncGet completed, read " << std::endl;
    return replicas.first; // version
}

void CacheAllocator::registerBuffer(std::string type, int segment_id, size_t base, size_t size) {
     // 创建新的 BufferAllocator 实例
    BufferAllocator new_allocator(type, segment_id, base, size);

    // 检查 type 是否存在
    auto type_it = buf_allocators_.find(type);
    if (type_it == buf_allocators_.end()) {
        // 如果 type 不存在，创建新的内层 map
        buf_allocators_[type] = std::map<int, std::vector<BufferAllocator>>();
    }

    // 检查 segment_id 是否存在
    auto& segment_map = buf_allocators_[type];
    auto segment_it = segment_map.find(segment_id);
    if (segment_it == segment_map.end()) {
        // 如果 segment_id 不存在，创建新的 vector
        segment_map[segment_id] = std::vector<BufferAllocator>();
    }

    // 添加新的 BufferAllocator 到 vector 中
    buf_allocators_[type][segment_id].push_back(std::move(new_allocator));
}

} // namespace mooncake