#include "distributed_object_store_helper.h"

const static size_t kLocalMemorySize = 1024 * 1024 * 1024;

PyDistributedObjectStore::PyDistributedObjectStore() {
    internal_ = new DistributedObjectStore();
    start_addr_ = internal_->allocateLocalMemory(kLocalMemorySize);
    if (!start_addr_)
        LOG(ERROR) << "Failed to allocate memory from transfer engine";
}

PyDistributedObjectStore::~PyDistributedObjectStore() {
    delete internal_;
    internal_ = nullptr;
}

int PyDistributedObjectStore::register_buffer(const std::string &segment_name,
                                              uint64_t start_addr,
                                              size_t size) {
    // TODO 判断失效的 segment_id & index
    uint64_t segment_id = 0;
    mutex_.lock();
    if (segment_id_lookup_.count(segment_name))
        segment_id = segment_id_lookup_[segment_name];
    else {
        segment_id = internal_->openSegment(segment_name);
        segment_id_lookup_[segment_name] = segment_id;
    }
    mutex_.unlock();
    internal_->registerBuffer(segment_id, start_addr, size);
    return 0;
}

uint64_t PyDistributedObjectStore::put_object(const std::string &key,
                                              const std::string &data) {
    std::lock_guard<std::mutex> guard(mutex_);
    Slice slice;
    memcpy(start_addr_, data.data(), data.size());
    slice.ptr = start_addr_;
    slice.size = data.size();
    ReplicateConfig config;
    config.replica_num = 1;
    auto putVersion = internal_->put(key, {slice}, config);
    TASK_STATUS status;
    while (true) {
        status = internal_->getTaskStatus(putVersion);
        if (status.first == OperationStatus::COMPLETE) {
            break;
        } else {
            std::this_thread::yield();
        }
    }
    return 0;
}

std::string PyDistributedObjectStore::get_object(const std::string &key,
                                                 size_t data_size,
                                                 uint64_t min_version,
                                                 uint64_t offset) {
    std::lock_guard<std::mutex> guard(mutex_);
    Slice slice;
    slice.ptr = start_addr_;
    slice.size = data_size;
    std::vector<Slice> slices = {slice};
    auto getVersion = internal_->get(key, slices, min_version, offset);
    TASK_STATUS status;
    while (true) {
        status = internal_->getTaskStatus(getVersion);
        if (status.first == OperationStatus::COMPLETE) {
            break;
        } else {
            std::this_thread::yield();
        }
    }
    return std::string((char *)start_addr_, data_size);
}

#ifdef USE_BOOST_PYTHON
#include <boost/python.hpp>
using namespace boost::python;
using namespace mooncake;

BOOST_PYTHON_MODULE(distributed_object_store) {
    class_<PyDistributedObjectStore>("DistributedObjectStore", init())
        .def("register_buffer", &PyDistributedObjectStore::register_buffer)
        .def("put_object", &PyDistributedObjectStore::put_object)
        .def("get_object", &PyDistributedObjectStore::get_object);
}
#else
#include <pybind11/pybind11.h>
using namespace mooncake;
namespace py = pybind11;

PYBIND11_MODULE(distributed_object_store, m) {
    py::class_<PyDistributedObjectStore>(m, "DistributedObjectStore")
        .def(py::init<>())
        .def("register_buffer", &PyDistributedObjectStore::register_buffer)
        .def("put_object", &PyDistributedObjectStore::put_object)
        .def("get_object", &PyDistributedObjectStore::get_object);
}

#endif