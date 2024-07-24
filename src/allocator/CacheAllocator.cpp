#include <string.h>

#include "CacheAllocator.h"

CacheAllocator::CacheAllocator(size_t shard_size, std::vector<std::unique_ptr<VirtualNode>> nodes, std::unique_ptr<AllocationStrategy> strategy)
    : shard_size(shard_size), virtual_nodes(std::move(nodes)), allocation_strategy(std::move(strategy)), global_version(0) {}

ReplicaList CacheAllocator::allocateReplicas(size_t obj_size, int num_replicas) {
    std::cout << "Allocating replicas for object size: " << obj_size << ", num replicas: " << num_replicas << std::endl;
    
    int num_shards = (obj_size + shard_size - 1) / shard_size;
    std::vector<int> selected_nodes = allocation_strategy->selectNodes(num_shards * num_replicas, num_replicas, virtual_nodes);
    
    std::cout << "Selected nodes: " << std::endl;
    for (int i = 0; i < num_replicas; ++i) {
        std::cout << "replicate " << i << " : " ;
        for (int j = 0; j < num_shards; ++j) {
            std::cout  << selected_nodes[i * num_shards + j] << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
    
    ReplicaList replicas(num_replicas);
    int node_index = 0;
    for (int i = 0; i < num_replicas; ++i) {
        std::cout << "Allocating replica " << i + 1 << ":" << std::endl;
        size_t remaining_size = obj_size;
        for (int j = 0; j < num_shards; ++j) {
            size_t shard_size = std::min(remaining_size, this->shard_size);
            BufHandle handle = virtual_nodes[selected_nodes[node_index]]->allocate(shard_size);
            replicas[i].handles.push_back(handle);
            std::cout << "  Shard " << j + 1 << ": Node " << selected_nodes[node_index] 
                      << ", Offset " << handle.offset << ", Size " << handle.size << std::endl;
            remaining_size -= shard_size;
            node_index++;
        }
        replicas[i].status = ReplicaStatus::INITIALIZED;
    }
    
    return replicas;
}

void CacheAllocator::writeDataToReplicas(ReplicaList& replicas, const std::vector<void*>& ptrs, const std::vector<void*>& sizes, int num_replicas) {
    for (int replica_idx = 0; replica_idx < num_replicas; ++replica_idx) {
        size_t written = 0;
        size_t input_offset = 0;
        size_t input_idx = 0;

        for (const auto& shard : replicas[replica_idx].handles) {
            char* dest = reinterpret_cast<char*>(virtual_nodes[shard.segment_id]->getBuffer(shard));
            size_t shard_offset = 0;

            while (shard_offset < shard.size && input_idx < ptrs.size()) {
                size_t input_size = reinterpret_cast<size_t>(sizes[input_idx]);
                size_t remaining_input = input_size - input_offset;
                size_t remaining_shard = shard.size - shard_offset;
                size_t to_write = std::min(remaining_input, remaining_shard);

                // 拷贝数据
                memcpy(dest + shard_offset, static_cast<char*>(ptrs[input_idx]) + input_offset, to_write);

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

        for (auto& shard : replicas[replica_idx].handles) {
            // 更新状态
            shard.status = BufStatus::COMPLETE;
        }
        replicas[replica_idx].status = ReplicaStatus::COMPLETED;
        std::cout << "Total written for replica " << replica_idx << ": " << written << " bytes" << std::endl;
    }
}


void CacheAllocator::updateObjectMeta(const ObjectKey& key, const ReplicaList& replicas, const ReplicateConfig& config) {
    Version new_version = ++global_version;
    auto& version_list = object_meta[key];
    version_list.versions[new_version] = replicas;
    version_list.flushed_version = new_version;
    version_list.config = config;
    
    std::cout << "Updated object meta for key: " << key << ", new version: " << new_version 
              << ", num replicas: " << config.num_replicas << std::endl;
}

std::pair<Version, ReplicaList> CacheAllocator::getReplicas(const ObjectKey& key, Version min_version) {
    std::cout << "Getting replicas for key: " << key << ", minimum version: " << min_version << std::endl;
    
    auto it = object_meta.find(key);
    if (it == object_meta.end()) {
        std::cout << "Object not found: " << key << std::endl;
        throw std::runtime_error("Object not found");
    }
    
    const auto& version_list = it->second;
    
    // 找到大于等于min_version的最新版本
    auto version_it = version_list.versions.lower_bound(min_version);
    if (version_it == version_list.versions.end()) {
        std::cout << "No version found greater than or equal to: " << min_version << std::endl;
        throw std::runtime_error("No suitable version found");
    }
    
    Version selected_version = version_it->first;
    std::cout << "Found replicas for version: " << selected_version << std::endl;
    return std::make_pair(version_it->first, version_it->second);
}

size_t CacheAllocator::readAndCopyData(const std::vector<BufHandle>& replica, 
                                       size_t offset, 
                                       std::vector<void*>& ptrs, 
                                       const std::vector<void*>& sizes) {
    size_t total_size = 0;
    for (const auto& size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }

    size_t current_offset = 0;
    size_t remaining_offset = offset; // offset in input
    size_t bytes_read = 0;
    size_t output_index = 0; // ptrs index
    size_t output_offset = 0; // offset in one ptr

    for (const auto& shard : replica) {
        if (current_offset + shard.size <= offset) {
            current_offset += shard.size;
            continue;
        }

        size_t shard_start = (remaining_offset > shard.size) ? 0 : remaining_offset;
        remaining_offset = (remaining_offset > shard.size) ? remaining_offset - shard.size : 0;

        char* shard_buffer = reinterpret_cast<char*>(virtual_nodes[shard.segment_id]->getBuffer(shard));

        while (shard_start < shard.size && bytes_read < total_size) {
            size_t bytes_to_read = std::min({
                shard.size - shard_start,
                reinterpret_cast<size_t>(sizes[output_index]) - output_offset,
                total_size - bytes_read
            });

            memcpy(static_cast<char*>(ptrs[output_index]) + output_offset,
                   shard_buffer + shard_start,
                   bytes_to_read);

            shard_start += bytes_to_read;
            bytes_read += bytes_to_read;
            output_offset += bytes_to_read;

            if (output_offset == reinterpret_cast<size_t>(sizes[output_index])) {
                output_index++;
                output_offset = 0;
            }
        }

        if (bytes_read >= total_size) {
            break;
        }
    }
    return bytes_read;
}


TaskID CacheAllocator::AsyncPut(ObjectKey key, PtrType type, std::vector<void*> ptrs, std::vector<void*> sizes, ReplicateConfig config) {
    if (ptrs.size() != sizes.size()) {
        throw std::invalid_argument("ptrs and sizes vectors must have the same length");
    }

    size_t total_size = 0;
    for (const auto& size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }
    std::cout << "AsyncPut: key=" << key << ", total_size=" << total_size << ", num_replicas=" << config.num_replicas << std::endl;
    
    ReplicaList replicas = allocateReplicas(total_size, config.num_replicas);
    writeDataToReplicas(replicas, ptrs, sizes, config.num_replicas);
    updateObjectMeta(key, replicas, config);
    
    std::cout << "AsyncPut completed, TaskID: " << global_version.load() << std::endl;
    return global_version.load();
}

TaskID CacheAllocator::AsyncReplicate(
    ObjectKey key, 
    ReplicateConfig new_config, 
    ReplicaDiff& replica_diff
) {
    std::cout << "AsyncReplicate: key=" << key << ", new_num_replicas=" << new_config.num_replicas << std::endl;
    
    auto it = object_meta.find(key);
    if (it == object_meta.end()) {
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
        replica_diff.added_replicas = new_replicas;
        current_replicas.insert(current_replicas.end(), new_replicas.begin(), new_replicas.end());
        // TODO: 开辟了空间 但没有复制数据
        replica_diff.change_status = ReplicaChangeStatus::ADDED;
    } else if (new_config.num_replicas < old_config.num_replicas) {
        // TODO : 这里删除时 暂时没有考虑副本有刚刚创建的情况，假设副本都是可用状态
        std::cout << "Removing " << old_config.num_replicas - new_config.num_replicas << " replicas" << std::endl;
        replica_diff.removed_replicas.assign(
            current_replicas.begin() + new_config.num_replicas,
            current_replicas.end()
        );
        for (int i = new_config.num_replicas; i < old_config.num_replicas; ++i) {
            for (const auto& handle : current_replicas[i].handles) {
                virtual_nodes[handle.segment_id]->deallocate(handle);
                std::cout << "Deallocated: Node " << handle.segment_id << ", Offset " << handle.offset << ", Size " << handle.size << std::endl;
            }
        }
        current_replicas.resize(new_config.num_replicas);
        replica_diff.change_status = ReplicaChangeStatus::REMOVED;
    } else {
        replica_diff.change_status = ReplicaChangeStatus::NO_CHANGE;
    }
    
    updateObjectMeta(key, current_replicas, new_config);
    
    std::cout << "AsyncReplicate completed, TaskID: " << global_version.load() << std::endl;
    return global_version.load();
}

TaskID CacheAllocator::AsyncGet(ObjectKey key, PtrType type, std::vector<void*> ptrs, std::vector<void*> sizes, Version min_version, size_t offset) {
    std::cout << "AsyncGet: key=" << key << ", min_version=" << min_version << ", offset=" << offset << std::endl;

    const std::pair<Version, ReplicaList>& replicas = getReplicas(key, min_version);

    size_t total_size = 0;
    for (const auto& size : sizes) {
        total_size += reinterpret_cast<size_t>(size);
    }

    std::cout << "Total size to read: " << total_size << " bytes" << std::endl;

    // 选择从第一个副本进行读取（可以根据需要实现更复杂的选择逻辑）
    size_t bytes_read = 0;
    for (const auto& replica : replicas.second) {
        if (replica.status == ReplicaStatus::COMPLETED) {
            std::cout << "Reading from replica with status COMPLETED" << std::endl;
            bytes_read = readAndCopyData(replica.handles, offset, ptrs, sizes);
            break;
        }
    }
    std::cout << "AsyncGet completed, read " << bytes_read << " bytes" << std::endl;
    return replicas.first; // version
}