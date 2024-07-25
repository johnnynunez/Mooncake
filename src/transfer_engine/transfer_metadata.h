// transfer_metadata.h
// Copyright (C) 2024 Feng Ren

#ifndef TRANSFER_METADATA
#define TRANSFER_METADATA

#include <atomic>
#include <thread>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <glog/logging.h>

#include <jsoncpp/json/json.h>
#include <libmemcached/memcached.hpp>

#include "transfer_engine/common.h"

namespace mooncake
{

    // 在 TransferEngine 运行期间需要元数据服务器或外部存储系统维护集群内各节点的可用性及 RDMA
    // 参数等， 相关逻辑由被此类抽象，便于与既有系统的集成。 当前基于 memcached/etcd 实现。

    /*
        ETCD/MEMCACHED 存放的节点元数据信息

        key = mooncake/[server_name]
        value = {
            'name': 'optane20'
            'devices': [
                { 'name': 'mlx5_2', 'lid': 17, 'gid': 'fe:00:...' },
                { 'name': 'mlx5_3', 'lid': 22, 'gid': 'fe:00:...' }
            ],
            'priority_matrix': {
                "cpu:0": [["mlx5_2"], ["mlx5_3"]],
                "cpu:1": [["mlx5_3"], ["mlx5_2"]],
                "cuda:0": [["mlx5_2"], ["mlx5_3"]],
            },
            'buffers': [
                {
                    'name': 'cpu:0',
                    'addr': 0x7fa16bdf5000,
                    'length': 1073741824,
                    'rkey': [1fe000, 1fdf00, ...],
                },
            ],
        }

        节点之间两两互传的 QP_NUM 信息

        value = {
            'local_nic_path': 'optane20@mlx5_2',
            'peer_nic_path': 'optane21@mlx5_2',
            'qp_num': [xxx, yyy]
        }
    */

    const static uint16_t kDefaultServerPort = 12001;

    struct TransferMetadataImpl;

    class TransferMetadata
    {
    public:
        struct DeviceDesc
        {
            std::string name;
            uint16_t lid;
            std::string gid;
        };

        struct BufferDesc
        {
            std::string name;
            uint64_t addr;
            uint64_t length;
            std::vector<uint32_t> rkey;
        };

        struct PriorityItem
        {
            std::vector<std::string> preferred_rnic_list;
            std::vector<std::string> available_rnic_list;
        };

        using PriorityMatrix = std::unordered_map<std::string, PriorityItem>;

        struct ServerDesc
        {
            std::string name;
            std::vector<DeviceDesc> devices;
            PriorityMatrix priority_matrix;
            std::vector<BufferDesc> buffers;
        };

        struct HandShakeDesc
        {
            std::string local_nic_path;
            std::string peer_nic_path;
            std::vector<uint32_t> qp_num;
        };

    public:
        TransferMetadata(const std::string &metadata_uri);

        ~TransferMetadata();

        int updateServerDesc(const std::string &server_name, const ServerDesc &desc);

        std::shared_ptr<ServerDesc> getServerDesc(const std::string &server_name);

        int removeServerDesc(const std::string &server_name);

        using OnReceiveHandShake = std::function<int(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)>;
        int startHandshakeDaemon(OnReceiveHandShake on_receive_handshake,
                                 uint16_t listen_port = kDefaultServerPort);

        int sendHandshake(const std::string &peer_server_name,
                          const HandShakeDesc &local_desc,
                          HandShakeDesc &peer_desc);

        static int parseNicPriorityMatrix(const std::string &nic_priority_matrix,
                                          PriorityMatrix &priority_map,
                                          std::vector<std::string> &rnic_list);

    private:
        std::string encode(const HandShakeDesc &desc);

        int decode(const std::string &ser, HandShakeDesc &desc);

    private:
        std::atomic<bool> listener_running_;
        std::thread listener_;
        OnReceiveHandShake on_receive_handshake_;

        std::shared_ptr<TransferMetadataImpl> impl_;
    };

}

#endif // TRANSFER_METADATA