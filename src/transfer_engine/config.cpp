// config.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/config.h"

namespace mooncake {
    void loadGlobalConfig(GlobalConfig &config)
    {
        const char* num_cq_per_ctx_env = std::getenv("MC_NUM_CQ_PER_CTX");
        if (num_cq_per_ctx_env)
        {
            int val = atoi(num_cq_per_ctx_env);
            if (val > 0 && val < 256) 
                config.num_cq_per_ctx = val;
        }

        const char* num_comp_channels_per_ctx_env = std::getenv("MC_NUM_COMP_CHANNELS_PER_CTX");
        if (num_comp_channels_per_ctx_env)
        {
            int val = atoi(num_comp_channels_per_ctx_env);
            if (val > 0 && val < 256) 
                config.num_comp_channels_per_ctx = val;
        }

        const char* port_env = std::getenv("MC_IB_PORT");
        if (port_env)
        {
            int val = atoi(port_env);
            if (val >= 0 && val < 256) 
                config.port = uint8_t(val);
        }
        
        const char* gid_index_env = std::getenv("MC_GID_INDEX");
        if (!gid_index_env)
            gid_index_env = std::getenv("NCCL_IB_GID_INDEX");

        if (gid_index_env)
        {
            int val = atoi(gid_index_env);
            if (val >= 0 && val < 256) 
                config.gid_index = val;
        }

        const char* max_cqe_per_ctx_env = std::getenv("MC_MAX_CQE_PER_CTX");
        if (max_cqe_per_ctx_env)
        {
            size_t val = atoi(max_cqe_per_ctx_env);
            if (val > 0 && val <= UINT16_MAX) 
                config.max_cqe = val;
        }

        const char* max_ep_per_ctx_env = std::getenv("MC_MAX_EP_PER_CTX");
        if (max_ep_per_ctx_env)
        {
            size_t val = atoi(max_ep_per_ctx_env);
            if (val > 0 && val <= UINT16_MAX) 
                config.max_ep_per_ctx = val;
        }

        const char* num_qp_per_ep_env = std::getenv("MC_NUM_QP_PER_EP");
        if (num_qp_per_ep_env)
        {
            int val = atoi(num_qp_per_ep_env);
            if (val > 0 && val < 256) 
                config.num_qp_per_ep = val;
        }

        const char* max_sge_env = std::getenv("MC_MAX_SGE");
        if (max_sge_env)
        {
            size_t val = atoi(max_sge_env);
            if (val > 0 && val <= UINT16_MAX) 
                config.max_sge = val;
        }

        const char* max_wr_env = std::getenv("MC_MAX_WR");
        if (max_wr_env)
        {
            size_t val = atoi(max_wr_env);
            if (val > 0 && val <= UINT16_MAX) 
                config.max_wr = val;
        }

        const char* max_inline_env = std::getenv("MC_MAX_INLINE");
        if (max_inline_env)
        {
            size_t val = atoi(max_inline_env);
            if (val > 0 && val <= UINT16_MAX) 
                config.max_inline = val;
        }

        const char* mtu_length_env = std::getenv("MC_MTU");
        if (mtu_length_env)
        {
            size_t val = atoi(mtu_length_env);
            if (val == 512) 
                config.mtu_length = IBV_MTU_512;
            else if (val == 1024) 
                config.mtu_length = IBV_MTU_1024;
            else if (val == 2048) 
                config.mtu_length = IBV_MTU_2048;
            else if (val == 4096) 
                config.mtu_length = IBV_MTU_4096;
            else 
            {
                LOG(ERROR) << "Unsupported MTU length, it should be 512|1024|2048|4096";
                exit(EXIT_FAILURE);
            }
        }
    }
}