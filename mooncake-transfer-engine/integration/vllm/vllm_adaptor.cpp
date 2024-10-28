#include "vllm_adaptor.h"

VLLMAdaptor::VLLMAdaptor() : next_free_(nullptr), managed_buffer_(nullptr) {}

VLLMAdaptor::~VLLMAdaptor() {
    for (auto &handle : handle_map_) engine_->closeSegment(handle.second);
    handle_map_.clear();
    engine_.reset();
    free(managed_buffer_);
    for (auto &entry : buffer_list_) free(entry);
    buffer_list_.clear();
}

int VLLMAdaptor::initialize(const char *local_hostname,
                            const char *metadata_server, const char *protocol,
                            const char *device_name) {
    auto metadata_client = std::make_shared<TransferMetadata>(metadata_server);
    if (!metadata_client) return -1;

    engine_ = std::make_unique<TransferEngine>(metadata_client);
    if (!engine_) return -1;

    auto hostname_port = parseHostNameWithPort(local_hostname);
    int ret = engine_->init(local_hostname, hostname_port.first.c_str(),
                            hostname_port.second);
    if (!engine_) return -1;

    xport_ = nullptr;
    if (strcmp(protocol, "rdma") == 0) {
        std::string nic_priority_matrix =
            "{\"cpu:0\": [[\"" + std::string(device_name) + "\"], []]}";
        void **args = (void **)malloc(2 * sizeof(void *));
        args[0] = (void *)nic_priority_matrix.c_str();
        args[1] = nullptr;
        xport_ = engine_->installOrGetTransport("rdma", args);
    } else if (strcmp(protocol, "tcp") == 0) {
        xport_ = engine_->installOrGetTransport("tcp", nullptr);
    } else {
        LOG(ERROR) << "Unsupported protocol";
        return -1;
    }

    if (!xport_) return -1;

    size_t capacity = kMaxBufferSize * kBufferCount;
    managed_buffer_ = malloc(capacity);
    if (!managed_buffer_) return -1;

    next_free_ = managed_buffer_;
    for (size_t i = 0; i < kBufferCount; ++i) {
        void **current =
            (void **)((char *)managed_buffer_ + i * kMaxBufferSize);
        void *next = i + 1 == kBufferCount
                         ? nullptr
                         : (char *)managed_buffer_ + (i + 1) * kMaxBufferSize;
        *current = next;
    }

    ret = engine_->registerLocalMemory(managed_buffer_, capacity, "cpu:0");
    if (ret) return -1;

    return 0;
}

uintptr_t VLLMAdaptor::allocateManagedBuffer(size_t length) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (length > kMaxBufferSize) {
        void *buffer = malloc(length);
        if (!buffer) return 0;
        int ret =
            engine_->registerLocalMemory(managed_buffer_, length, "cpu:0");
        if (ret) {
            free(buffer);
            return 0;
        }
        buffer_list_.insert(buffer);
        return (uintptr_t)buffer;
    }
    if (!next_free_) return 0;
    auto buffer = next_free_;
    next_free_ = *(void **)next_free_;
    return (uintptr_t)buffer;
}

int VLLMAdaptor::freeManagedBuffer(uintptr_t buffer_addr, size_t length) {
    void *buffer = (void *)buffer_addr;
    std::lock_guard<std::mutex> guard(mutex_);
    if (length > kMaxBufferSize) {
        engine_->unregisterLocalMemory(buffer);
        buffer_list_.erase(buffer);
        free(buffer);
        return 0;
    }

    int i = ((uint64_t)buffer - (uint64_t)managed_buffer_) / kMaxBufferSize;
    void *fixed_buffer = (char *)managed_buffer_ + i * kMaxBufferSize;
    *(void **)fixed_buffer = next_free_;
    *(void **)next_free_ = fixed_buffer;
    return 0;
}

int VLLMAdaptor::transferSync(const char *target_hostname, uintptr_t buffer,
                              uintptr_t peer_buffer_address, size_t length) {
    if ((uintptr_t)buffer < (uintptr_t)managed_buffer_ ||
        (uintptr_t)buffer >
            (uintptr_t)managed_buffer_ + kBufferCount * kMaxBufferSize) {
        LOG(ERROR) << "buffer must be managed";
        return -1;
    }

    Transport::SegmentHandle handle;
    if (handle_map_.count(target_hostname)) {
        handle = handle_map_[target_hostname];
    } else {
        handle = engine_->openSegment(target_hostname);
        if (handle == (Transport::SegmentHandle)-1) return -1;
        handle_map_[target_hostname] = handle;
    }

    auto batch_id = xport_->allocateBatchID(1);
    TransferRequest entry;
    entry.opcode = TransferRequest::READ;
    entry.length = length;
    entry.source = (void *)buffer;
    entry.target_id = handle;
    entry.target_offset = peer_buffer_address;

    int ret = xport_->submitTransfer(batch_id, {entry});
    if (ret < 0) return -1;

    TransferStatus status;
    while (true) {
        int ret = xport_->getTransferStatus(batch_id, 0, status);
        LOG_ASSERT(!ret);
        if (status.s == TransferStatusEnum::COMPLETED) {
            xport_->freeBatchID(batch_id);
            return 0;
        } else if (status.s == TransferStatusEnum::FAILED) {
            xport_->freeBatchID(batch_id);
            return -1;
        }
    }
}

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(mooncake_vllm_adaptor, m) {
    py::class_<VLLMAdaptor>(m, "mooncake_vllm_adaptor")
        .def(py::init<>())
        .def("initialize", &VLLMAdaptor::initialize)
        .def("allocateManagedBuffer", &VLLMAdaptor::allocateManagedBuffer)
        .def("freeManagedBuffer", &VLLMAdaptor::freeManagedBuffer)
        .def("transferSync", &VLLMAdaptor::transferSync);
}