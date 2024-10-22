#include "distributed_object_store.h"

#include <map>
#include <mutex>

using namespace mooncake;

class PyDistributedObjectStore
{
public:
    PyDistributedObjectStore();

    ~PyDistributedObjectStore();

    // 返回0表示成功，负数表示错误
    int register_buffer(const std::string &segment_name, uint64_t start_addr, size_t size);

    // 返回0表示失败，否则表示 version id
    uint64_t put_object(const std::string &key, const std::string &data);

    // 返回获取到的对象数据长度，如果失败则返回空字符串
    std::string get_object(const std::string &key, size_t data_size, uint64_t min_version, uint64_t offset);

private:
    DistributedObjectStore *internal_;
    void *start_addr_;
    std::mutex mutex_;
    std::map<std::string, SegmentId> segment_id_lookup_;
};