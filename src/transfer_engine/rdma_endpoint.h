// rdma_endpoint.h
// Copyright (C) 2024 Feng Ren

#ifndef RDMA_ENDPOINT_H
#define RDMA_ENDPOINT_H

#include "transfer_engine/rdma_context.h"
#include "transfer_engine/transfer_engine.h"

#include <queue>

namespace mooncake
{
    // RdmaEndPoint 表示本地 NIC1 (由所属的 RdmaContext 确定) 与远端 NIC2 (由 peer_nic_path 确定) 之间的所有 QP 连接
    // 构造完毕后（RdmaEndPoint::RdmaEndPoint() 和 RdmaEndPoint::construct()），相应资源被分配，但不指定对端
    //
    // 随后需要与对手方交换 Handshake 信息，RdmaEndPoint 才能正确发送工作请求。
    //
    // 主动发起握手的一方，调用 setupConnectionsByActive 函数，传入对端的 peer_nic_path
    // peer_nic_path := peer_server_name@nic_name，如 optane20@mlx5_3，可由对端 RdmaContext::nicPath() 取得
    //
    // 被动触发握手的一方，由 TransferMetadata 监听线程调用 setupConnectionsByPassive 函数。对端的 peer_nic_path
    // 位于 peer_desc.local_nic_path 内
    //
    // 完成上述操作后，RdmaEndPoint 状态被设置为 CONNECTED
    //
    // 用户主动调用 disconnect() 或内部检测到错误时，连接作废，RdmaEndPoint 状态被设置为 UNCONNECTED
    // 此时可以重新触发握手流程
    class RdmaEndPoint
    {
    public:
        enum Status
        {
            INITIALIZING,
            UNCONNECTED,
            CONNECTED,
        };

    public:
        RdmaEndPoint(RdmaContext &context);

        ~RdmaEndPoint();

        // 进一步构造 ibv_qp * 等内部对象，需要在调用 RdmaEndPoint::RdmaEndPoint() 构造函数后调用其一次
        int construct(ibv_cq *cq,
                      size_t num_qp_list = 2,
                      size_t max_sge = 4,
                      size_t max_wr = 256,
                      size_t max_inline = 64);

    private:
        // 被 RdmaEndPoint::~RdmaEndPoint() 自动调用
        int deconstruct();

    public:
        void setPeerNicPath(const std::string &peer_nic_path);

        int setupConnectionsByActive();

        int setupConnectionsByActive(const std::string &peer_nic_path)
        {
            setPeerNicPath(peer_nic_path);
            return setupConnectionsByActive();
        }

        using HandShakeDesc = TransferMetadata::HandShakeDesc;
        int setupConnectionsByPassive(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc);

        bool hasOutstandingSlice() const;
        
        bool active() const { return active_; }

        void set_active(bool flag) { active_ = flag; }

    public:
        // 连接状态管理

        // 连接建立返回 true，否则返回 false
        bool connected() const
        {
            return status_.load(std::memory_order_relaxed) == CONNECTED;
        }

        // 中断连接，可由用户触发或者内部检错逻辑调用，在途的传输将会被中断
        // 调用此函数后，可再次调用 setupConnections 系列函数恢复连接
        // 下一次可以连接到不同的远端 NIC（本地 NIC 固定）
        void disconnect();

        // 只有 QP 被 destroy 之后，与之关联的 CQ 才能被 destroy
        // 在 RdmaContext 的析构函数 destroy CQ 之前需要先手动调用该函数 destroy 掉 QP
        int destroyQP();

    private:
        void disconnectUnlocked();

    public:
        const std::string toString() const;

    public:
        // 提交并执行其中的部分工作请求，已提交的任务会从 slice_list 中删除，提交失败的任务会加入 failed_slice_list。
        int submitPostSend(std::vector<TransferEngine::Slice *> &slice_list,
                           std::vector<TransferEngine::Slice *> &failed_slice_list);

    private:
        std::vector<uint32_t> qpNum() const;

        int doSetupConnection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list, std::string *reply_msg = nullptr);

        int doSetupConnection(int qp_index, const std::string &peer_gid, uint16_t peer_lid, uint32_t peer_qp_num, std::string *reply_msg = nullptr);

    private:
        RdmaContext &context_;
        std::atomic<Status> status_;

        RWSpinlock lock_;
        std::vector<ibv_qp *> qp_list_;

        std::string peer_nic_path_;

        volatile int *wr_depth_list_;
        int max_wr_depth_;
        
        volatile bool active_;
    };

}

#endif // RDMA_ENDPOINT_H