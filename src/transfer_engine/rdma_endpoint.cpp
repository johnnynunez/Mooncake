// rdma_endpoint.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/rdma_endpoint.h"
#include "transfer_engine/config.h"

#include <cassert>
#include <cstddef>
#include <glog/logging.h>

namespace mooncake
{
    const static uint8_t MAX_HOP_LIMIT = 16;
    const static uint8_t TIMEOUT = 14;
    const static uint8_t RETRY_CNT = 7;

    RdmaEndPoint::RdmaEndPoint(RdmaContext &context)
        : context_(context),
          status_(INITIALIZING),
          posted_slice_count_(0) {}

    RdmaEndPoint::~RdmaEndPoint()
    {
        if (!qp_list_.empty())
            deconstruct();
    }

    int RdmaEndPoint::construct(ibv_cq *cq,
                                size_t num_qp_list,
                                size_t max_sge_per_wr,
                                size_t max_wr_depth,
                                size_t max_inline_bytes)
    {
        if (status_.load(std::memory_order_relaxed) != INITIALIZING) {
            PLOG(ERROR) << "Endpoint has already been constructed";
            return -1;
        }

        qp_list_.resize(num_qp_list);

        max_wr_depth_ = (int)max_wr_depth;
        wr_depth_list_ = new volatile int[num_qp_list];
        if (!wr_depth_list_)
        {
            PLOG(ERROR) << "Failed to allocate memory for work request depth list";
            return -1;
        }
        for (size_t i = 0; i < num_qp_list; ++i)
        {
            wr_depth_list_[i] = 0;
            ibv_qp_init_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.send_cq = cq;
            attr.recv_cq = cq;
            attr.sq_sig_all = false;
            attr.qp_type = IBV_QPT_RC;
            attr.cap.max_send_wr = attr.cap.max_recv_wr = max_wr_depth;
            attr.cap.max_send_sge = attr.cap.max_recv_sge = max_sge_per_wr;
            attr.cap.max_inline_data = max_inline_bytes;
            qp_list_[i] = ibv_create_qp(context_.pd(), &attr);
            if (!qp_list_[i])
            {
                PLOG(ERROR) << "Failed to create QP";
                return -1;
            }
        }

        status_.store(UNCONNECTED, std::memory_order_relaxed);
        return 0;
    }

    int RdmaEndPoint::deconstruct()
    {
        for (size_t i = 0; i < qp_list_.size(); ++i) {
            if (ibv_destroy_qp(qp_list_[i])) {
                PLOG(ERROR) << "Failed to destroy QP";
                return -1;
            }
        }
        qp_list_.clear();
        delete[] wr_depth_list_;
        return 0;
    }

    int RdmaEndPoint::destroyQP()
    {
        return deconstruct();
    }

    void RdmaEndPoint::setPeerNicPath(const std::string &peer_nic_path)
    {
        RWSpinlock::WriteGuard guard(lock_);
        if (connected())
        {
            LOG(WARNING) << "Previous connection is discarded";
            disconnectUnlocked();
        }
        peer_nic_path_ = peer_nic_path;
    }

    int RdmaEndPoint::setupConnectionsByActive()
    {
        RWSpinlock::WriteGuard guard(lock_);
        if (connected())
        {
            LOG(WARNING) << "Connection already connected";
            return 0;
        }
        HandShakeDesc local_desc, peer_desc;
        local_desc.local_nic_path = context_.nicPath();
        local_desc.peer_nic_path = peer_nic_path_;
        local_desc.qp_num = qpNum();

        auto peer_server_name = getServerNameFromNicPath(peer_nic_path_);
        auto peer_nic_name = getNicNameFromNicPath(peer_nic_path_);
        if (peer_server_name.empty() || peer_nic_name.empty())
        {
            LOG(ERROR) << "Parse peer nic path failed: " << peer_nic_path_;
            return -1;
        }

        int rc = context_.engine().sendHandshake(peer_server_name, local_desc, peer_desc);
        if (rc)
        {
            LOG(ERROR) << "Failed to exchange handshake description";
            return rc;
        }

        if (peer_desc.local_nic_path != peer_nic_path_ || peer_desc.peer_nic_path != local_desc.local_nic_path)
        {
            LOG(ERROR) << "Invalid argument: received packet mismatch";
            return -1;
        }

        auto &nic_list = context_.engine().getSegmentDescByName(peer_server_name)->devices;
        for (auto &nic : nic_list)
            if (nic.name == peer_nic_name)
                return doSetupConnection(nic.gid, nic.lid, peer_desc.qp_num);

        LOG(ERROR) << "Peer NIC " << peer_nic_name << " not found in " << peer_server_name;
        return -1;
    }

    int RdmaEndPoint::setupConnectionsByPassive(const HandShakeDesc &peer_desc, HandShakeDesc &local_desc)
    {
        RWSpinlock::WriteGuard guard(lock_);
        if (connected())
        {
            LOG(WARNING) << "Discard connection: " << toString();
            disconnectUnlocked();
        }

        peer_nic_path_ = peer_desc.local_nic_path;
        if (peer_desc.peer_nic_path != context_.nicPath())
        {
            LOG(ERROR) << "Invalid argument: received packet mismatch";
            return -1;
        }

        auto peer_server_name = getServerNameFromNicPath(peer_nic_path_);
        auto peer_nic_name = getNicNameFromNicPath(peer_nic_path_);
        if (peer_server_name.empty() || peer_nic_name.empty())
        {
            LOG(ERROR) << "Parse peer nic path failed: " << peer_nic_path_;
            return -1;
        }

        local_desc.local_nic_path = context_.nicPath();
        local_desc.peer_nic_path = peer_nic_path_;
        local_desc.qp_num = qpNum();

        auto &nic_list = context_.engine().getSegmentDescByName(peer_server_name)->devices;
        for (auto &nic : nic_list)
            if (nic.name == peer_nic_name)
                return doSetupConnection(nic.gid, nic.lid, peer_desc.qp_num);

        LOG(ERROR) << "Peer NIC " << peer_nic_name << " not found in " << peer_server_name;
        return -1;
    }

    void RdmaEndPoint::disconnect()
    {
        RWSpinlock::WriteGuard guard(lock_);
        disconnectUnlocked();
    }

    void RdmaEndPoint::disconnectUnlocked()
    {
        ibv_qp_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RESET;
        for (auto &qp : qp_list_)
        {
            if (ibv_modify_qp(qp, &attr, IBV_QP_STATE))
                PLOG(ERROR) << "Failed to modity QP to RESET";
        }

        peer_nic_path_.clear();
        for (size_t i = 0; i < qp_list_.size(); ++i)
            wr_depth_list_[i] = 0;
        posted_slice_count_.store(0, std::memory_order_relaxed);
        status_.store(UNCONNECTED, std::memory_order_release);
    }

    const std::string RdmaEndPoint::toString() const
    {
        auto status = status_.load(std::memory_order_relaxed);
        if (status == CONNECTED)
            return "EndPoint: local " + context_.nicPath() + ", peer " + peer_nic_path_;
        else
            return "EndPoint: local " + context_.nicPath() + " (unconnected)";
    }

    int RdmaEndPoint::submitPostSend(std::vector<TransferEngine::Slice *> &slice_list,
                                     std::vector<TransferEngine::Slice *> &failed_slice_list)
    {
        RWSpinlock::WriteGuard guard(lock_);
        int qp_index = SimpleRandom::Get().next(qp_list_.size());
        int wr_count = std::min(max_wr_depth_ - wr_depth_list_[qp_index], (int)slice_list.size());
        if (wr_count == 0)
            return 0;

        ibv_send_wr wr_list[wr_count], *bad_wr = nullptr;
        ibv_sge sge_list[wr_count];
        memset(wr_list, 0, sizeof(ibv_send_wr) * wr_count);
        for (int i = 0; i < wr_count; ++i)
        {
            auto slice = slice_list[i];
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
            slice->status = TransferEngine::Slice::POSTED;
            slice->rdma.qp_depth = &wr_depth_list_[qp_index];
            __sync_fetch_and_add(slice->rdma.qp_depth, 1);
        }

        int rc = ibv_post_send(qp_list_[qp_index], wr_list, &bad_wr);
        if (rc)
        {
            PLOG(ERROR) << "ibv_post_send failed";
            for (int i = 0; i < wr_count; ++i)
            {
                failed_slice_list.push_back(slice_list[i]);
                __sync_fetch_and_sub(slice_list[i]->rdma.qp_depth, 1);
            }
            slice_list.erase(slice_list.begin(), slice_list.begin() + wr_count);
            return rc;
        }

        posted_slice_count_.fetch_add(wr_count, std::memory_order_relaxed);
        slice_list.erase(slice_list.begin(), slice_list.begin() + wr_count);
        return 0;
    }

    std::vector<uint32_t> RdmaEndPoint::qpNum() const
    {
        std::vector<uint32_t> ret;
        for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
            ret.push_back(qp_list_[qp_index]->qp_num);
        return ret;
    }

    int RdmaEndPoint::doSetupConnection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list)
    {
        if (qp_list_.size() != peer_qp_num_list.size())
        {
            LOG(ERROR) << "Invalid argument";
            return -1;
        }

        for (int qp_index = 0; qp_index < (int)qp_list_.size(); ++qp_index)
            if (doSetupConnection(qp_index, peer_gid, peer_lid, peer_qp_num_list[qp_index]))
                return -1;

        status_.store(CONNECTED, std::memory_order_relaxed);
        return 0;
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
        attr.port_num = context_.portNum();
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
        attr.path_mtu = context_.activeMTU();
        if (globalConfig().mtu_length < attr.path_mtu)
            attr.path_mtu = globalConfig().mtu_length;
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
        // TODO gidIndex and portNum must fetch from REMOTE
        attr.ah_attr.grh.sgid_index = context_.gidIndex();
        attr.ah_attr.grh.hop_limit = MAX_HOP_LIMIT;
        attr.ah_attr.dlid = peer_lid;
        attr.ah_attr.sl = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.ah_attr.static_rate = 0;
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = context_.portNum();
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
}