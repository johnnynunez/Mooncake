// rdma_test.cpp
// Copyright (C) 2024 Feng Ren

#include "net/transfer_engine.h"

#include <thread>
#include <gtest/gtest.h>

TEST(transfer_engine, basic_test)
{
    auto metadata_client = std::make_unique<TransferMetadata>("optane21:12345");
    ASSERT_TRUE(metadata_client);

    const std::string nic_priority_matrix = "{ \"cpu\": [\"mlx5_2\", \"mlx5_3\"] }";
    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client,
                                                   "optane21",
                                                   dram_buffer_size,
                                                   0,
                                                   nic_priority_matrix);
    ASSERT_TRUE(engine);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
