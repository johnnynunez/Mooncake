// transfer_metadata.h
// Copyright (C) 2024 Feng Ren

#ifndef TRANSFER_METADATA
#define TRANSFER_METADATA

#include <glog/logging.h>
#include <jsoncpp/json/json.h>
#include <netdb.h>

#include <atomic>
#include <cstdint>
#include <etcd/SyncClient.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "common.h"

namespace mooncake {
const static uint16_t kDefaultServerPort = 12001;

struct TransferMetadataImpl;

class TransferMetadata {
   public:
    struct DeviceDesc {
        std::string name;
        uint16_t lid;
        std::string gid;
    };

    struct BufferDesc {
        std::string name;
        uint64_t addr;
        uint64_t length;
        std::vector<uint32_t> lkey;
        std::vector<uint32_t> rkey;
    };

    struct NVMeoFBufferDesc {
        std::string file_path;
        uint64_t length;
        std::unordered_map<std::string, std::string> local_path_map;
    };

    struct PriorityItem {
        std::vector<std::string> preferred_rnic_list;
        std::vector<std::string> available_rnic_list;
        std::vector<int> preferred_rnic_id_list;
        std::vector<int> available_rnic_id_list;
    };

    using PriorityMatrix = std::unordered_map<std::string, PriorityItem>;
    using SegmentID = uint64_t;

    struct SegmentDesc {
        std::string name;
        std::string protocol;
        // this is for rdma
        std::vector<DeviceDesc> devices;
        PriorityMatrix priority_matrix;
        std::vector<BufferDesc> buffers;
        // this is for nvmeof.
        std::vector<NVMeoFBufferDesc> nvmeof_buffers;
        // TODO : make these two a union or a std::variant
    };

    struct HandShakeDesc {
        std::string local_nic_path;
        std::string peer_nic_path;
        std::vector<uint32_t> qp_num;
        std::string reply_msg;  // 该字段非空表示握手过程期间发生错误
    };

   public:
    TransferMetadata(const std::string &metadata_uri);

    ~TransferMetadata();

    // TODO: rename functions
    std::shared_ptr<SegmentDesc> getSegmentDescByName(
        const std::string &segment_name, bool force_update = false);

    std::shared_ptr<SegmentDesc> getSegmentDescByID(SegmentID segment_id,
                                                    bool force_update = false);

    int updateLocalSegmentDesc(SegmentID segment_id = LOCAL_SEGMENT_ID);

    int updateSegmentDesc(const std::string &server_name,
                          const SegmentDesc &desc);

    std::shared_ptr<SegmentDesc> getSegmentDesc(const std::string &server_name);

    SegmentID getSegmentID(const std::string &server_name);

    int syncSegmentCache();

    int removeSegmentDesc(const std::string &server_name);

    int addLocalMemoryBuffer(const BufferDesc &buffer_desc,
                             bool update_metadata);

    int removeLocalMemoryBuffer(void *addr, bool update_metadata);

    int addLocalSegment(SegmentID segment_id, const string &server_name,
                        std::shared_ptr<SegmentDesc> &&desc);

    using OnReceiveHandShake = std::function<int(const HandShakeDesc &peer_desc,
                                                 HandShakeDesc &local_desc)>;
    int startHandshakeDaemon(OnReceiveHandShake on_receive_handshake,
                             uint16_t listen_port = kDefaultServerPort);

    int sendHandshake(const std::string &peer_server_name,
                      const HandShakeDesc &local_desc,
                      HandShakeDesc &peer_desc);

    static int parseNicPriorityMatrix(const std::string &nic_priority_matrix,
                                      PriorityMatrix &priority_map,
                                      std::vector<std::string> &rnic_list);

   private:
    int doSendHandshake(struct addrinfo *addr, const HandShakeDesc &local_desc,
                        HandShakeDesc &peer_desc);

    std::string encode(const HandShakeDesc &desc);

    int decode(const std::string &ser, HandShakeDesc &desc);

   private:
    std::atomic<bool> listener_running_;
    std::thread listener_;
    OnReceiveHandShake on_receive_handshake_;
    // local cache
    RWSpinlock segment_lock_;
    std::unordered_map<uint64_t, std::shared_ptr<SegmentDesc>>
        segment_id_to_desc_map_;
    std::unordered_map<std::string, uint64_t> segment_name_to_id_map_;

    std::atomic<SegmentID> next_segment_id_;

    std::shared_ptr<TransferMetadataImpl> impl_;
};

}  // namespace mooncake

#endif  // TRANSFER_METADATA