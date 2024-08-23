// config.h
// Copyright (C) 2024 Feng Ren

#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <glog/logging.h>
#include <jsoncpp/json/json.h>
#include <infiniband/verbs.h>

namespace mooncake
{
    struct GlobalConfig {
        size_t num_cq_per_ctx = 1;
        size_t num_comp_channels_per_ctx = 1;
        uint8_t port = 1;
        int gid_index = 3;
        size_t max_cqe = 4096;
        int max_ep_per_ctx = 256;
        size_t num_qp_per_ep = 2;
        size_t max_sge = 4;
        size_t max_wr = 256;
        size_t max_inline = 64;
        ibv_mtu mtu_length = IBV_MTU_4096;
        uint16_t handshake_port = 12001;
        int workers_per_ctx = 2;
        bool verbose = false;
        size_t slice_size = 65536;
        int retry_cnt = 8;
    };

    void loadGlobalConfig(GlobalConfig &config);

    void dumpGlobalConfig();

    void updateGlobalConfig(ibv_device_attr &device_attr);

    static inline GlobalConfig &globalConfig() {
        static GlobalConfig config;
        static std::once_flag g_once_flag;
        std::call_once(g_once_flag, []() {
            loadGlobalConfig(config);
        });
        return config;
    }
}

#endif // CONFIG_H