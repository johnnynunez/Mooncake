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

class TransportTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // 初始化 glog
        google::InitGoogleLogging("TransportTest");
        FLAGS_logtostderr = 1;  // 将日志输出到标准错误
    }

    void TearDown() override {
        // 清理 glog
        google::ShutdownGoogleLogging();
    }
};


// 创建临时文件，并返回文件描述符
static int CreateTempFile() {
    char temp_filename[] = "/tmp/testfileXXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        return -1;
    }
    unlink(temp_filename);  // 确保程序退出后文件被删除
    return fd;
}

// 创建临时文件，并写入一些测试数据
int CreateTempFileWithContent(const char *content) {
    char temp_filename[] = "/tmp/testfileXXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        return -1;
    }
    unlink(temp_filename); // 确保程序退出后文件被删除

    write(fd, content, strlen(content));
    lseek(fd, 0, SEEK_SET); // 重置文件指针

    return fd;
}

TEST_F(TransportTest, parseHostNameWithPortTest) {
    std::string local_server_name = "0.0.0.0:1234";
    auto res = parseHostNameWithPort(local_server_name);
    ASSERT_EQ(res.first, "0.0.0.0");
    ASSERT_EQ(res.second, 1234);

    local_server_name = "1.2.3.4:111111";
    res = parseHostNameWithPort(local_server_name);
    ASSERT_EQ(res.first, "1.2.3.4");
    ASSERT_EQ(res.second, 12001);
}

// 测试写操作成功
TEST_F(TransportTest, WriteSuccess) {
    int fd = CreateTempFile();
    ASSERT_NE(fd, -1) << "Failed to create temporary file";

    const char* testData = "Hello, World!";
    size_t testDataLen = strlen(testData);

    ssize_t result = writeFully(fd, testData, testDataLen);
    EXPECT_EQ(result, testDataLen);

    char buffer[256] = {0};
    lseek(fd, 0, SEEK_SET);  // 回到文件开头
    read(fd, buffer, testDataLen);
    EXPECT_STREQ(buffer, testData);

    close(fd);
}

// 测试写操作失败（无效的文件描述符）
TEST_F(TransportTest, WriteInvalidFD) {
    const char* testData = "Hello, World!";
    size_t testDataLen = strlen(testData);

    ssize_t result = writeFully(-1, testData, testDataLen);
    ASSERT_EQ(result, -1);
    ASSERT_EQ(errno, EBADF);  // 检查 errno 是否设置为无效文件描述符错误
}

// 测试部分写入
TEST_F(TransportTest, PartialWrite) {
    int fd = CreateTempFile();
    ASSERT_NE(fd, -1) << "Failed to create temporary file";

    const char* testData = "Hello, World!";
    size_t testDataLen = strlen(testData);

    ssize_t result = writeFully(fd, testData, testDataLen / 2);

    ASSERT_EQ(result, testDataLen / 2);

    char buffer[256] = {0};
    lseek(fd, 0, SEEK_SET);  // 回到文件开头
    read(fd, buffer, result);
    ASSERT_EQ(strncmp(buffer, testData, result), 0);
    close(fd);
}

// 测试读取操作成功
TEST_F(TransportTest, ReadSuccess) {
    const char *testData = "Hello, World!";
    int fd = CreateTempFileWithContent(testData);
    ASSERT_NE(fd, -1) << "Failed to create temporary file";

    char buffer[256] = {0};
    ssize_t bytesRead = readFully(fd, buffer, sizeof(buffer));

    EXPECT_EQ(bytesRead, strlen(testData));
    EXPECT_STREQ(buffer, testData);

    close(fd);
}

// 测试读取操作失败（无效的文件描述符）
TEST_F(TransportTest, ReadInvalidFD) {
    char buffer[256] = {0};
    ssize_t bytesRead = readFully(-1, buffer, sizeof(buffer));
    EXPECT_EQ(bytesRead, -1);
    EXPECT_EQ(errno, EBADF); // 检查 errno 是否设置为无效文件描述符错误
}

// 测试读取部分数据
TEST_F(TransportTest, PartialRead) {
    const char *testData = "Hello, World!";
    int fd = CreateTempFileWithContent(testData);
    ASSERT_NE(fd, -1) << "Failed to create temporary file";

    char buffer[256] = {0};
    size_t half_len = strlen(testData) / 2;
    ssize_t bytesRead = readFully(fd, buffer, half_len);

    EXPECT_EQ(bytesRead, half_len);
    EXPECT_EQ(strncmp(buffer, testData, half_len), 0);

    close(fd);
}

// 测试读取空文件
TEST_F(TransportTest, ReadEmptyFile) {
    int fd = CreateTempFileWithContent("");
    ASSERT_NE(fd, -1) << "Failed to create temporary file";

    char buffer[256] = {0};
    ssize_t bytesRead = readFully(fd, buffer, sizeof(buffer));

    EXPECT_EQ(bytesRead, 0);

    close(fd);
}
}  // namespace mooncake

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}