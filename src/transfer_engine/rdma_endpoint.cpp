// rdma_endpoint.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/rdma_endpoint.h"

namespace mooncake
{
    const static uint8_t MAX_HOP_LIMIT = 16;
    const static uint8_t TIMEOUT = 14;
    const static uint8_t RETRY_CNT = 7;

    RdmaEndPoint::RdmaEndPoint(RdmaContext *context,
                               const std::string &local_nic_path,
                               const std::string &peer_nic_path)
        : local_nic_path_(local_nic_path),
          peer_nic_path_(peer_nic_path),
          context_(context),
          slice_queue_size_(0),
          post_slice_count_(0),
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

        LOG(INFO) << "Constructed new EndPoint: local " << local_nic_path_
                  << ", peer " << peer_nic_path_;
        return 0;
    }

    int RdmaEndPoint::deconstruct()
    {
        for (size_t i = 0; i < qp_list_.size(); ++i)
            ibv_destroy_qp(qp_list_[i]);
        qp_list_.clear();
        return 0;
    }

    int RdmaEndPoint::doSetupConnection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list)
    {
        if (qp_list_.size() != peer_qp_num_list.size())
        {
            LOG(ERROR) << "Invalid argument";
            return -1;
        }

        int ret = 0;
        for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
            if (doSetupConnection(qp_index, peer_gid, peer_lid, peer_qp_num_list[qp_index]))
                ret = -1;
            else
                connected_ = true;

        return ret;
    }

    int RdmaEndPoint::doSetupConnection(int qp_index, const std::string &peer_gid, uint16_t peer_lid, uint32_t peer_qp_num)
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
        attr.port_num = context_->portNum();
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
        attr.ah_attr.grh.sgid_index = context_->gidIndex();
        attr.ah_attr.grh.hop_limit = MAX_HOP_LIMIT;
        attr.ah_attr.dlid = peer_lid;
        attr.ah_attr.sl = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.ah_attr.static_rate = 0;
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = context_->portNum();
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

    int RdmaEndPoint::postSend(int qp_index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)
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

    uint32_t RdmaEndPoint::qpNum(int qp_index) const
    {
        if (qp_index < 0 || qp_index > (int)qp_list_.size())
            return -1;
        return qp_list_[qp_index]->qp_num;
    }

    std::vector<uint32_t> RdmaEndPoint::qpNum() const
    {
        std::vector<uint32_t> ret;
        for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
            ret.push_back(qp_list_[qp_index]->qp_num);
        return ret;
    }

    int RdmaEndPoint::queueDepthEstimate() const
    {
        int sum = 0;
        for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
            sum += qp_depth_list_[qp_index];
        return sum;
    }

    int RdmaEndPoint::submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list)
    {
        RWSpinlock::WriteGuard guard(lock_);
        slice_queue_size_.fetch_add(slice_list.size(), std::memory_order_release);
        for (auto &slice : slice_list)
            slice_queue_.emplace(slice);
        post_slice_count_.fetch_add(slice_list.size(), std::memory_order_relaxed);
        context_->notifySenderThread();
        return 0;
    }

    int RdmaEndPoint::performPostSend()
    {
        if (slice_queue_size_.load(std::memory_order_acquire) == 0)
            return 0;

        int posted_slice_count = 0;
        int qp_index = lrand48() % qp_list_.size();
        while (qp_depth_list_[qp_index] < max_qp_depth_)
        {
            std::vector<TransferEngine::Slice *> slice_list;

            lock_.WLock();
            int wr_count = std::min(max_qp_depth_ - qp_depth_list_[qp_index], (int)slice_queue_.size());
            for (int i = 0; i < wr_count; ++i)
            {
                auto slice = slice_queue_.front();
                slice_queue_.pop();
                slice_list.push_back(slice);
            }
            lock_.WUnlock();
            if (wr_count == 0)
                break;

            ibv_send_wr wr_list[wr_count], *bad_wr = nullptr;
            ibv_sge sge_list[wr_count];
            memset(wr_list, 0, sizeof(ibv_send_wr) * wr_count);
            for (int i = 0; i < wr_count; ++i)
            {
                auto slice = slice_list[i];
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

            int rc = postSend(qp_index, wr_list, &bad_wr);
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

    bool RdmaEndPoint::connected()
    {
        RWSpinlock::ReadGuard guard(lock_);
        return connected_;
    }

    void RdmaEndPoint::reset()
    {
        RWSpinlock::WriteGuard guard(lock_);
        connected_ = false;
    }

    int RdmaEndPoint::setupConnectionsByActive()
    {
        RWSpinlock::WriteGuard guard(lock_);
        HandShakeDesc local_desc, peer_desc;

        local_desc.local_nic_path = local_nic_path_;
        local_desc.peer_nic_path = peer_nic_path_;
        local_desc.qp_num = qpNum();

        auto peer_server_name = getServerNameFromNicPath(peer_nic_path_);
        auto peer_nic_name = getNicNameFromNicPath(peer_nic_path_);

        int rc = context_->engine()->sendHandshake(peer_server_name, local_desc, peer_desc);
        if (rc)
            return rc;

        if (peer_desc.local_nic_path != peer_nic_path_ || peer_desc.peer_nic_path != local_nic_path_)
        {
            LOG(ERROR) << "Invalid argument";
            return -1;
        }

        auto &nic_list = context_->engine()->getSegmentDescByName(peer_server_name)->devices;
        for (auto &nic : nic_list)
            if (nic.name == peer_nic_name)
                return doSetupConnection(nic.gid, nic.lid, peer_desc.qp_num);

        LOG(INFO) << "NIC " << peer_nic_name << " not found in server desc";
        return -1;
    }

    int RdmaEndPoint::setupConnectionsByPassive(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        RWSpinlock::WriteGuard guard(lock_);
        if (peer_desc.local_nic_path != peer_nic_path_ || peer_desc.peer_nic_path != local_nic_path_)
        {
            LOG(ERROR) << "Invalid argument";
            return -1;
        }

        auto peer_server_name = getServerNameFromNicPath(peer_nic_path_);
        auto peer_nic_name = getNicNameFromNicPath(local_nic_path_);

        local_desc.local_nic_path = local_nic_path_;
        local_desc.peer_nic_path = peer_nic_path_;
        local_desc.qp_num = qpNum();

        auto &nic_list = context_->engine()->getSegmentDescByName(peer_server_name)->devices;
        for (auto &nic : nic_list)
            if (nic.name == peer_nic_name)
                return doSetupConnection(nic.gid, nic.lid, peer_desc.qp_num);

        LOG(INFO) << "NIC " << peer_nic_name << " not found in server desc";
        return -1;
    }
}