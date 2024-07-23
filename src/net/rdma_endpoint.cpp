// rdma_endpoint.cpp
// Copyright (C) 2024 Feng Ren

#include "net/rdma_endpoint.h"

const static uint8_t MAX_HOP_LIMIT = 16;
const static uint8_t TIMEOUT = 14;
const static uint8_t RETRY_CNT = 7;

RdmaEndPoint::RdmaEndPoint(RdmaContext *context, const std::string &server_name)
    : server_name_(server_name),
      context_(context),
      slice_queue_size_(0),
      connected_(false) {}

RdmaEndPoint::~RdmaEndPoint()
{
    if (!qp_list_.empty())
        deconstruct();
}

int RdmaEndPoint::construct(ibv_cq *cq,
                            size_t num_qp_list,
                            size_t max_sge,
                            size_t max_wr,
                            size_t max_inline)
{
    if (connected_)
    {
        LOG(INFO) << "Endpoint has been connected";
        return -1;
    }
    max_qp_depth_ = (int)max_wr;
    qp_list_.resize(num_qp_list);
    qp_depth_list_.resize(num_qp_list, 0);
    for (size_t i = 0; i < num_qp_list; ++i)
    {
        ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.send_cq = cq;
        attr.recv_cq = cq;
        attr.sq_sig_all = false;
        attr.qp_type = IBV_QPT_RC;
        attr.cap.max_send_wr = attr.cap.max_recv_wr = max_wr;
        attr.cap.max_send_sge = attr.cap.max_recv_sge = max_sge;
        attr.cap.max_inline_data = max_inline;
        qp_list_[i] = ibv_create_qp(context_->pd(), &attr);
        if (!qp_list_[i])
        {
            PLOG(ERROR) << "Failed to create QP";
            return -1;
        }
    }
    return 0;
}

int RdmaEndPoint::deconstruct()
{
    for (size_t i = 0; i < qp_list_.size(); ++i)
        ibv_destroy_qp(qp_list_[i]);
    qp_list_.clear();
    return 0;
}

int RdmaEndPoint::setup_connection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list)
{
    if (connected_)
    {
        LOG(INFO) << "Endpoint has been connected";
        return -1;
    }
    RWSpinlock::WriteGuard guard(lock_);
    if (qp_list_.size() != peer_qp_num_list.size())
        return -1;
    for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
        if (setup_connection(qp_index, peer_gid, peer_lid, peer_qp_num_list[qp_index]))
            return -1;
    connected_ = true;
    return 0;
}

int RdmaEndPoint::setup_connection(int qp_index, const std::string &peer_gid, uint16_t peer_lid, uint32_t peer_qp_num)
{
    if (qp_index < 0 || qp_index > (int)qp_list_.size())
        return -1;
    auto &qp = qp_list_[qp_index];

    // Any state -> RESET
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RESET;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE))
    {
        PLOG(ERROR) << "Failed to modity QP to RESET";
        return -1;
    }

    // RESET -> INIT
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = context_->port_num();
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
    {
        PLOG(ERROR) << "Failed to modity QP to INIT";
        return -1;
    }

    // INIT -> RTR
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096; // TODO Does it supported by RoCE2?
    ibv_gid peer_gid_raw;
    std::istringstream iss(peer_gid);
    for (int i = 0; i < 16; ++i)
    {
        int value;
        iss >> std::hex >> value;
        peer_gid_raw.raw[i] = static_cast<uint8_t>(value);
        if (i < 15)
            iss.ignore(1, ':');
    }
    attr.ah_attr.grh.dgid = peer_gid_raw;
    attr.ah_attr.grh.sgid_index = context_->gid_index();
    attr.ah_attr.grh.hop_limit = MAX_HOP_LIMIT;
    attr.ah_attr.dlid = peer_lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.static_rate = 0;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.port_num = context_->port_num();
    attr.dest_qp_num = peer_qp_num;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 16;
    attr.min_rnr_timer = 12; // 12 in previous implementation
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_MIN_RNR_TIMER | IBV_QP_AV | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN))
    {
        PLOG(ERROR) << "Failed to modity QP to RTR";
        return -1;
    }

    // RTR -> RTS
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = TIMEOUT;
    attr.retry_cnt = RETRY_CNT;
    attr.rnr_retry = 7; // or 7,RNR error
    attr.sq_psn = 0;
    attr.max_rd_atomic = 16;

    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC))
    {
        PLOG(ERROR) << "Failed to modity QP to RTS";
        return -1;
    }

    return 0;
}

int RdmaEndPoint::post_send(int qp_index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)
{
    if (qp_index < 0 || qp_index > (int)qp_list_.size() || !connected_)
        return -1;
    auto &qp = qp_list_[qp_index];
    int ret = ibv_post_send(qp, wr, bad_wr);
    if (ret)
    {
        PLOG(ERROR) << "Failed to post work request";
        return ret;
    }
    return 0;
}

uint32_t RdmaEndPoint::qp_num(int qp_index) const
{
    if (qp_index < 0 || qp_index > (int)qp_list_.size())
        return -1;
    return qp_list_[qp_index]->qp_num;
}

std::vector<uint32_t> RdmaEndPoint::qp_num() const
{
    std::vector<uint32_t> ret;
    for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
        ret.push_back(qp_list_[qp_index]->qp_num);
    return ret;
}

int RdmaEndPoint::queue_depth_estimate() const
{
    int sum = 0;
    for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
        sum += qp_depth_list_[qp_index];
    return sum;
}

int RdmaEndPoint::submit_post_send(const std::vector<TransferEngine::Slice *> &slice_list)
{
    RWSpinlock::WriteGuard guard(lock_);
    slice_queue_size_.fetch_add(slice_list.size(), std::memory_order_release);
    for (auto &slice : slice_list)
        slice_queue_.emplace(slice);
    return 0;
}

int RdmaEndPoint::perform_post_send()
{
    if (slice_queue_size_.load(std::memory_order_acquire) == 0)
        return 0;

    RWSpinlock::WriteGuard guard(lock_);
    int posted_slice_count = 0;
    int qp_index = lrand48() % qp_list_.size();
    while (!slice_queue_.empty() && qp_depth_list_[qp_index] < max_qp_depth_)
    {
        std::vector<TransferEngine::Slice *> slice_list;
        int wr_count = std::min(max_qp_depth_ - qp_depth_list_[qp_index], (int)slice_queue_.size());
        ibv_send_wr wr_list[wr_count], *bad_wr = nullptr;
        ibv_sge sge_list[wr_count];

        memset(wr_list, 0, sizeof(ibv_send_wr) * wr_count);
        for (int i = 0; i < wr_count; ++i)
        {
            auto slice = slice_queue_.front();
            slice_queue_.pop();
            slice_list.push_back(slice);
            posted_slice_count++;

            auto &sge = sge_list[i];
            sge.addr = (uint64_t)slice->source_addr;
            sge.length = slice->length;
            sge.lkey = slice->rdma.source_lkey;

            auto &wr = wr_list[i];
            wr.wr_id = (uint64_t)slice;
            wr.opcode = slice->opcode == TransferEngine::TransferRequest::READ ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
            wr.num_sge = 1;
            wr.sg_list = &sge;
            wr.send_flags = IBV_SEND_SIGNALED;
            wr.next = (i + 1 == wr_count) ? nullptr : &wr_list[i + 1];
            wr.imm_data = 0;
            wr.wr.rdma.remote_addr = slice->rdma.dest_addr;
            wr.wr.rdma.rkey = slice->rdma.dest_rkey;
            slice->status.store(TransferEngine::Slice::POSTED, std::memory_order_release);
            slice->rdma.qp_depth = &qp_depth_list_[qp_index];
            qp_depth_list_[qp_index]++;
        }

        int rc = post_send(qp_index, wr_list, &bad_wr);
        if (rc)
        {
            PLOG(ERROR) << "post send failed";
            for (int i = 0; i < wr_count; ++i)
                slice_list[i]->status = TransferEngine::Slice::FAILED;
            exit(-1);
        }
    }

    slice_queue_size_.fetch_sub(posted_slice_count, std::memory_order_relaxed);
    return 0;
}
