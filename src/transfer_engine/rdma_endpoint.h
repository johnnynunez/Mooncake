// rdma_endpoint.h
// Copyright (C) 2024 Feng Ren

#ifndef RDMA_ENDPOINT_H
#define RDMA_ENDPOINT_H

#include "transfer_engine/rdma_context.h"
#include "transfer_engine/transfer_engine.h"

#include <queue>

namespace mooncake
{
    class RdmaEndPoint
    {
    public:
        // 每个 RdmaEndPoint 表示本地 NIC1 与远端 NIC2 之间的一个（或多个） QP 连接
        // 传入远端的 nic_path := server_name@nic_name
        // nic_name 是系统内核看到的 NIC 名字，如 mlx5_2
        RdmaEndPoint(RdmaContext *context, const std::string &local_nic_path_, const std::string &peer_nic_path_);

        ~RdmaEndPoint();

        int construct(ibv_cq *cq,
                      size_t num_qp_list = 2,
                      size_t max_sge = 4,
                      size_t max_wr = 256,
                      size_t max_inline = 64);

        int deconstruct();

        // 完成构造函数（包含 construct 后）RdmaEndPoint 不能立即使用，需要与对手方交换 Handshake
        // 信息才能使用。主动发起握手的一方，调用 setupConnectionsByActive 函数
        // 被动触发握手的一方，由 TransferMetadata 监听线程调用 setupConnectionsByPassive 函数。
        // 完成该操作后，connected() 被设为 true
        int setupConnectionsByActive();

        using HandShakeDesc = TransferMetadata::HandShakeDesc;
        int setupConnectionsByPassive(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc);

    public:
        // getter
        bool connected();

        uint32_t qpNum(int qp_index) const;

        std::vector<uint32_t> qpNum() const;

        const std::string localNicPath() const { return local_nic_path_; }

        const std::string peerNicPath() const { return peer_nic_path_; }

        int queueDepthEstimate() const;

        int postSliceCount() const { return post_slice_count_.load(); }

        // setter
        void reset();

    public:
        // 由 TransferEngine 调用，向队列添加 Slice
        int submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list);

        // 由 RdmaContext 后台线程调用，从队列提取 Slices 并执行 ibv_post_send()
        int performPostSend();

    private:
        int postSend(int qp_index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);

        int doSetupConnection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list);

        int doSetupConnection(int qp_index, const std::string &peer_gid, uint16_t peer_lid, uint32_t peer_qp_num);

    private:
        const std::string local_nic_path_;
        const std::string peer_nic_path_;

        RWSpinlock lock_;
        RdmaContext *context_;
        std::vector<ibv_qp *> qp_list_;
        std::queue<TransferEngine::Slice *> slice_queue_;
        std::atomic<int> slice_queue_size_;
        std::atomic<uint64_t> post_slice_count_;
        std::vector<int> qp_depth_list_;

        int max_qp_depth_;
        bool connected_;
    };

}

#endif // RDMA_ENDPOINT_H