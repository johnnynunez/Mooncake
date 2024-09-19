// transfer_engine.h
// Copyright (C) 2024 Feng Ren

#ifndef TRANSFER_ENGINE
#define TRANSFER_ENGINE

#include <atomic>
#include <cstddef>
#include <infiniband/verbs.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "transfer_metadata.h"
#include "transport/transport.h"

// const int LOCAL_SEGMENT_ID = 0;

namespace mooncake
{

    class RdmaContext;
    class RdmaEndPoint;
    class TransferMetadata;
    class WorkerPool;

    // OldTransferEngine
    class RdmaTransport: public Transport
    {
        friend class RdmaContext;
        friend class RdmaEndPoint;
        friend class WorkerPool;

    public:
        using BufferDesc = TransferMetadata::BufferDesc;
        using SegmentDesc = TransferMetadata::SegmentDesc;
        using HandShakeDesc = TransferMetadata::HandShakeDesc;

    public:
        // --- 构造与析构 ---

        // 每个 CLIENT 启动时需构造一个 OldTransferEngine 实例。
        // - metadata：TransferMetadata 对象指针，该对象将 OldTransferEngine
        //   框架与元数据服务器/etcd 等带外通信逻辑抽取出来，以方便用户将其部署到不同的环境中。
        // - local_server_name：标识本地 CLIENT 的名称。集群内 local_server_name 的值应当具备唯一性。
        //   推荐使用 hostname。
        // - dram_buffer_size：OldTransferEngine 启动时分配的 DRAM 存储空间池大小。
        // - vram_bufffer_size：OldTransferEngine 启动时分配的 VRAM 存储空间池大小。
        // - nic_priority_matrix：是一个 JSON 字符串，表示使用的存储介质名称及优先使用的网卡列表
        // RdmaTransport(std::unique_ptr<TransferMetadata> &metadata,
        //                const std::string &local_server_name,
        //                const std::string &nic_priority_matrix,
        //                bool dry_run = false);

        RdmaTransport();

        // 回收分配的所有类型资源。
        ~RdmaTransport();

        int install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args) override;

        const char *getName() const override { return "rdma"; }

        // 本地用户缓冲区管理
        //
        // 可以在 Transfer 操作期间被用作读写缓冲区（位于本地）使用，也可作为数据源（即 Segment）
        // 被其他 CLIENT 利用。

        // 在本地 DRAM/VRAM 上注册起始地址为 addr，长度为 size 的空间。
        // - addr: 注册空间起始地址；
        // - length：注册空间长度；
        // - location：这块内存所处的位置提示，如 cpu:0 等，将用于和 PriorityMatrix 匹配可用的 RNIC 列表
        // - 返回值：若成功，返回 0；否则返回负数值。
        int registerLocalMemory(void *addr, size_t length, const std::string &location, bool remote_accessible, bool update_metadata) override;

        // 解注册区域。
        // - addr: 注册空间起始地址；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int unregisterLocalMemory(void *addr, bool update_metadata = true) override;

        int registerLocalMemoryBatch(const std::vector<BufferEntry> &buffer_list, const std::string &location) override;

        int unregisterLocalMemoryBatch(const std::vector<void *> &addr_list) override;

        // TRANSFER

        int submitTransfer(BatchID batch_id,
                           const std::vector<TransferRequest> &entries) override;

        // 获取 batch_id 对应所有 TransferRequest 的运行状态。
        // - batch_id: 所属的 BatchID ；
        // - status: Transfer 状态数组（输出）；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int getTransferStatus(BatchID batch_id,
                              std::vector<TransferStatus> &status);

        int getTransferStatus(BatchID batch_id, size_t task_id,
                              TransferStatus &status) override;

        // 获取 segment_name 对应的 SegmentID，其中 segment_name 在 RDMA 语义中表示目标服务器的名称 (与 server_name 相同)
        // TODO: Delete These Functions
        SegmentID getSegmentID(const std::string &segment_name);

    private:
        int allocateLocalSegmentID(TransferMetadata::PriorityMatrix &priority_matrix);

    public:
        // std::shared_ptr<SegmentDesc> getSegmentDescByName(const std::string &segment_name, bool force_update = false);

        // std::shared_ptr<SegmentDesc> getSegmentDescByID(SegmentID segment_id, bool force_update = false);

        // int updateLocalSegmentDesc();

        // int removeLocalSegmentDesc();

        // 为实现 RDMA 通联，需要将新 Segment 所属 CLIENT 与集群内原有 CLIENT 之间建立
        // QP 配对，以建立点对点可靠连接。subscribe_segment() 调用方将发出 RPC 请求至新 Segment 所属 CLIENT
        // （即 owner_server_name），后者调用此接口推进连接的建立操作。该接口不应被最终用户主动调用。
        // 1. 在简化部署模式下，该功能通过一个特别简单的 TCP-based RPC 服务实现。
        // 2. 在完整的 Mooncake Store 中，该函数在执行 SetupRDMAConnections RPC 期间被直接调用。
        // - request_qp_reg_desc：传入的请求方（远程）每张卡 RDMA QP 注册标识信息（LID、GID、QPN）
        // - response_qp_reg_desc：传出的响应方（本地）每张卡 RDMA QP 注册标识信息（LID、GID、QPN）
        // - 返回值：若成功，返回 0；否则返回负数值。
        int onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc);

        int sendHandshake(const std::string &peer_server_name,
                          const HandShakeDesc &local_desc,
                          HandShakeDesc &peer_desc)
        {
            return metadata_->sendHandshake(peer_server_name, local_desc, peer_desc);
        }

    private:
        int initializeRdmaResources();

        int startHandshakeDaemon(std::string &local_server_name);

    public:
        static int selectDevice(SegmentDesc *desc, uint64_t offset, size_t length, int &buffer_id, int &device_id, int retry_cnt = 0);

    private:

        std::vector<std::string> device_name_list_;
        std::vector<std::shared_ptr<RdmaContext>> context_list_;
        std::unordered_map<std::string, int> device_name_to_index_map_;
        std::atomic<SegmentID> next_segment_id_;

        // RWSpinlock segment_lock_;
        // std::unordered_map<SegmentID, std::shared_ptr<SegmentDesc>> segment_id_to_desc_map_;
        // std::unordered_map<std::string, SegmentID> segment_name_to_id_map_;

        // RWSpinlock batch_desc_lock_;
        // std::unordered_map<BatchID, std::shared_ptr<BatchDesc>> batch_desc_set_;
    };

    using TransferRequest = Transport::TransferRequest;
    using TransferStatus = Transport::TransferStatus;
    using TransferStatusEnum = Transport::TransferStatusEnum;
    using SegmentID = Transport::SegmentID;
    using BatchID = Transport::BatchID;

}

#endif // TRANSFER_ENGINE