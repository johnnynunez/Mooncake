// rdma_endpoint.h
// Copyright (C) 2024 Feng Ren

#ifndef RDMA_ENDPOINT_H
#define RDMA_ENDPOINT_H

#include "net/rdma_context.h"
#include "net/transfer_engine.h"

#include <queue>

class RdmaEndPoint
{
public:
    RdmaEndPoint(RdmaContext *context, const std::string &server_name);

    ~RdmaEndPoint();

    int construct(ibv_cq *cq,
                  size_t num_qp_list = 2,
                  size_t max_sge = 4,
                  size_t max_wr = 256,
                  size_t max_inline = 64);

    int deconstruct();

    int setup_connection(const std::string &peer_gid, uint16_t peer_lid, std::vector<uint32_t> peer_qp_num_list);

    int post_send(int qp_index, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);

    uint32_t qp_num(int qp_index) const;

    std::vector<uint32_t> qp_num() const;

    int queue_depth_estimate() const;

    bool connected() const { return connected_; }

    void reset() { connected_ = false; }

    const std::string server_name() const { return server_name_; }

    int submit_post_send(const std::vector<TransferEngine::Slice *> &slice_list);

    int perform_post_send();

private:
    int setup_connection(int qp_index, const std::string &peer_gid, uint16_t peer_lid, uint32_t peer_qp_num);

private:
    const std::string server_name_;
    RWSpinlock lock_;
    RdmaContext *context_;
    std::vector<ibv_qp *> qp_list_;
    std::queue<TransferEngine::Slice *> slice_queue_;
    std::atomic<int> slice_queue_size_;
    std::vector<int> qp_depth_list_;

    int max_qp_depth_;
    bool connected_;
};

#endif // RDMA_ENDPOINT_H