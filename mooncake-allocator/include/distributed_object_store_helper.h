// Copyright 2024 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <map>
#include <mutex>

#include "distributed_object_store.h"

using namespace mooncake;

class PyDistributedObjectStore {
   public:
    PyDistributedObjectStore();

    ~PyDistributedObjectStore();

    // 返回0表示成功，负数表示错误
    int register_buffer(const std::string &segment_name, uint64_t start_addr,
                        size_t size);

    // 返回0表示失败，否则表示 version id
    uint64_t put_object(const std::string &key, const std::string &data);

    // 返回获取到的对象数据长度，如果失败则返回空字符串
    std::string get_object(const std::string &key, size_t data_size,
                           uint64_t min_version, uint64_t offset);

   private:
    DistributedObjectStore *internal_;
    void *start_addr_;
    std::mutex mutex_;
    std::map<std::string, SegmentId> segment_id_lookup_;
};