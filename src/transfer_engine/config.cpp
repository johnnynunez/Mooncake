// config.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/config.h"

namespace mooncake {
    void loadGlobalConfig(GlobalConfig &config)
    {
        const char* gid_index_env = std::getenv("NCCL_IB_GID_INDEX");
        if (gid_index_env)
        {
            int val = atoi(gid_index_env);
            if (val >= 0 && val < 256) 
                config.gid_index = val;
        }

        const char* port_env = std::getenv("MC_IB_PORT");
        if (port_env)
        {
            int val = atoi(port_env);
            if (val >= 0 && val < 256) 
                config.port = uint8_t(val);
        }

        const char* num_qp_per_ep_env = std::getenv("MC_NUM_QP_PER_EP");
        if (num_qp_per_ep_env)
        {
            int val = atoi(num_qp_per_ep_env);
            if (val > 0 && val < 256) 
                config.num_qp_per_ep = val;
        }
    }
}