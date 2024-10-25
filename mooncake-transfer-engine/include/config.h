// config.h
// Copyright (C) 2024 Feng Ren

#ifndef CONFIG_H
#define CONFIG_H

#include <glog/logging.h>
#include <infiniband/verbs.h>
#include <jsoncpp/json/json.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace mooncake {
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

GlobalConfig &globalConfig();

uint16_t getDefaultHandshakePort();
}  // namespace mooncake

#endif  // CONFIG_H