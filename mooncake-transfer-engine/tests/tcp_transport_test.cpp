#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <sys/time.h>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>

#include "transfer_engine.h"
#include "transport/transport.h"

using namespace mooncake;

namespace mooncake {

class TCPTransportTest : public ::testing::Test {
   public:
   protected:
    void SetUp() override {
        static int offset = 0;
        LOG(INFO) << "HERE \n";

        google::InitGoogleLogging("TCPTransportTest");
        FLAGS_logtostderr = 1;  
        
    }

    void TearDown() override {
        // 清理 glog
        google::ShutdownGoogleLogging();
    }
};

TEST_F(TCPTransportTest, GetTcpTest) {
    auto metadata_client =
        std::make_shared<TransferMetadata>("127.0.0.1:2379");
    LOG_ASSERT(metadata_client);

    const size_t ram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client);

    auto hostname_port = parseHostNameWithPort("127.0.0.2:12345");
    engine->init("127.0.0.2:12345", hostname_port.first.c_str(),
                 hostname_port.second);
    LOG_ASSERT(engine->installOrGetTransport("tcp", nullptr));
}

}  // namespace mooncake

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}