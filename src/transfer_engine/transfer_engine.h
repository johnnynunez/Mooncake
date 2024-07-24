// transfer_engine.h
// Copyright (C) 2024 Feng Ren

#ifndef TRANSFER_ENGINE
#define TRANSFER_ENGINE

#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <infiniband/verbs.h>

#include "transfer_engine/transfer_metadata.h"

namespace mooncake
{

    class RdmaContext;
    class RdmaEndPoint;
    class TransferMetadata;

    // TransferEngine
    class TransferEngine
    {
        friend class RdmaContext;
        friend class RdmaEndPoint;

    public:
        enum DeviceType
        {
            DRAM = 1,
            VRAM = 2,
            NVMEOF = 10,
        };
        using SegmentID = int32_t;
        using BatchID = uint64_t;
        const static BatchID INVALID_BATCH_ID = UINT64_MAX;

        struct TransferRequest
        {
            enum OpCode
            {
                READ,
                WRITE
            };

            OpCode opcode;
            void *source;        // 应当是当前 TransferEngine 管理的 DRAM/VRAM buffer，
                                 // 或者由 allocate_local_memory 或 register_local_memory 分配
            SegmentID target_id; // 形如 *server_name*/vram:X 或 *server_name*/dram
            size_t target_offset;
            size_t length;
        };

        enum TransferStatusEnum
        {
            WAITING,
            PENDING,
            INVALID,
            CANNELED,
            COMPLETED,
            TIMEOUT,
            FAILED
        };

        struct TransferStatus
        {
            TransferStatusEnum s;
            size_t transferred_bytes;
        };

        using SegmentDesc = TransferMetadata::SegmentDesc;
        using HandShakeDesc = TransferMetadata::HandShakeDesc;

    public:
        // --- 构造与析构 ---

        // 每个 CLIENT 启动时需构造一个 TransferEngine 实例。
        // - metadata：TransferMetadata 对象指针，该对象将 TransferEngine
        //   框架与元数据服务器/etcd 等带外通信逻辑抽取出来，以方便用户将其部署到不同的环境中。
        // - local_server_name：标识本地 CLIENT 的名称。集群内 local_server_name 的值应当具备唯一性。
        //   推荐使用 hostname。
        // - dram_buffer_size：TransferEngine 启动时分配的 DRAM 存储空间池大小。
        // - vram_bufffer_size：TransferEngine 启动时分配的 VRAM 存储空间池大小。
        // - nic_priority_matrix：是一个 JSON 字符串，表示使用的存储介质名称及优先使用的网卡列表
        TransferEngine(std::unique_ptr<TransferMetadata> &metadata,
                       const std::string &local_server_name,
                       size_t dram_buffer_size,
                       size_t vram_buffer_size,
                       const std::string &nic_priority_matrix);

        // 回收分配的所有类型资源。
        ~TransferEngine();

        // 本地用户内存管理
        //
        // 本地用户内存可以在 Transfer 操作期间被用作读写缓冲区（位于本地）使用，也可作为数据源（即 Segment）
        // 被其他 CLIENT 利用。

        // 在本地 DRAM/VRAM 上注册起始地址为 addr，长度为 size 的空间。
        // - addr: 注册空间起始地址；
        // - size：注册空间长度；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int registerLocalMemory(void *addr, size_t size);

        // 解注册区域。若该区域是由 allocate_local_memory() 分配的，则同时回收内存空间。
        // - addr: 注册空间起始地址；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int unregisterLocalMemory(void *addr);

        // TRANSFER

        // 分配 BatchID。同一 BatchID 下最多可提交 batch_size 个 TransferRequest。
        // - batch_size: 同一 BatchID 下最多可提交的 TransferRequest 数量；
        // - 返回值：若成功，返回 BatchID（非负）；否则返回负数值。
        BatchID allocateBatchID(size_t batch_size);

        // 向 batch_id 追加提交新的 Transfer 任务。同一 batch_id 下累计的 entries 数量不应超过创建时定义的
        // batch_size。
        // - batch_id: 所属的 BatchID ；
        // - entries: Transfer 任务数组；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int submitTransfer(BatchID batch_id,
                           const std::vector<TransferRequest> &entries);

        // 获取 batch_id 对应所有 TransferRequest 的运行状态。
        // - batch_id: 所属的 BatchID ；
        // - status: Transfer 状态数组（输出）；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int getTransferStatus(BatchID batch_id,
                              std::vector<TransferStatus> &status);

        // 回收 BatchID，之后对此的 submit_transfer 及 get_transfer_status 操作是未定义的。该序号后续可能会被重新使用。
        // - batch_id: 所属的 BatchID ；
        // - 返回值：若成功，返回 0；否则返回负数值。
        int freeBatchID(BatchID batch_id);

        // Helper functions
        void *get_dram_buffer() const { return dram_buffer_list_[0]; }

        SegmentID getSegmentID(const std::string &segment_path);

        int UpdateRnicLinkSpeed(const std::vector<int> &rnic_speed);

    public:
        int joinCluster();

        int leaveCluster();

        // 在执行 subscribe_segment() 期间，为实现 RDMA 通联，需要将新 Segment 所属 CLIENT 与集群内原有 CLIENT 之间建立
        // QP 配对，以建立点对点可靠连接。subscribe_segment() 调用方将发出 RPC 请求至新 Segment 所属 CLIENT
        // （即 owner_server_name），后者调用此接口推进连接的建立操作。该接口不应被最终用户主动调用。
        // 1. 在简化部署模式下，该功能通过一个特别简单的 TCP-based RPC 服务实现。
        // 2. 在完整的 Mooncake Store 中，该函数在执行 SetupRDMAConnections RPC 期间被直接调用。
        // - request_qp_reg_desc：传入的请求方（远程）每张卡 RDMA QP 注册标识信息（LID、GID、QPN）
        // - response_qp_reg_desc：传出的响应方（本地）每张卡 RDMA QP 注册标识信息（LID、GID、QPN）
        // - 返回值：若成功，返回 0；否则返回负数值。
        int onSetupRdmaConnections(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc);

    private:
        int allocateInternalBuffer();

        int freeInternalBuffer();

        int parseNicPriorityMatrix(const std::string &nic_priority_matrix);

        int initializeRdmaResources();

        int startHandshakeDaemon();

        int connectServer(const std::string &remote_server_name);

    private:
        struct TransferTask;

        struct Slice
        {
            enum SliceStatus
            {
                PENDING,
                POSTED,
                SUCCESS,
                TIMEOUT,
                FAILED
            };

            void *source_addr;
            size_t length;
            TransferRequest::OpCode opcode;

            union
            {
                struct
                {
                    uint64_t dest_addr;
                    uint32_t source_lkey;
                    uint32_t dest_rkey;
                    int rkey_index;
                    int *qp_depth;
                } rdma;
                struct
                {
                    void *dest_addr;
                } local;
                struct
                {
                    // TBD
                } nvmeof;
            };

            std::atomic<SliceStatus> status;
            TransferTask *task;
        };

        struct TransferTask
        {
            TransferTask()
                : success_slice_count(0),
                  failed_slice_count(0),
                  transferred_bytes(0),
                  total_bytes(0) {}

            ~TransferTask()
            {
                for (auto item : slices)
                    delete item;
            }

            std::vector<Slice *> slices;
            volatile uint64_t success_slice_count;
            volatile uint64_t failed_slice_count;
            volatile uint64_t transferred_bytes;
            uint64_t total_bytes;
        };

        struct BatchDesc
        {
            BatchID id;
            size_t batch_size;
            std::vector<TransferTask> task_list;
        };

    private:
        std::unique_ptr<TransferMetadata> metadata_;

        using PriorityMap = TransferMetadata::PriorityMap;
        PriorityMap priority_map_;
        std::vector<std::string> rnic_list_;
        std::vector<uint8_t> rnic_prob_list_; // possibility to use this rnic
        std::vector<std::shared_ptr<RdmaContext>> context_list_;

        RWSpinlock connected_server_lock_;
        std::unordered_set<std::string> connected_server_set_;

        RWSpinlock segment_lock_;
        std::unordered_map<SegmentID, SegmentDesc *> segment_desc_map_;
        std::unordered_map<std::string, SegmentID> segment_lookup_map_;
        std::atomic<SegmentID> next_segment_id_;

        RWSpinlock batch_desc_lock_;
        std::unordered_set<BatchDesc *> batch_desc_set_;

        std::vector<void *> dram_buffer_list_;
        std::vector<void *> vram_buffer_list_;

        const std::string local_server_name_;
        const size_t dram_buffer_size_, vram_buffer_size_;
    };

    using TransferRequest = TransferEngine::TransferRequest;
    using TransferStatus = TransferEngine::TransferStatus;
    using TransferStatusEnum = TransferEngine::TransferStatusEnum;
    using SegmentID = TransferEngine::SegmentID;
    using BatchID = TransferEngine::BatchID;

}

#endif // TRANSFER_ENGINE