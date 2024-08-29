#include "transfer_engine.h"
#include "transport/rdma_transport/rdma_transport.h"
#include "transport/transport.h"
#ifdef USE_CUDA
#include "transport/nvmeof_transport/nvmeof_transport.h"
#endif

namespace mooncake
{
    static bool overlap(const void *a, size_t a_len, const void *b, size_t b_len)
    {
        return (a >= b && a < (char*)b + b_len) || (b >= a && b < (char*)a + a_len);
    }

    int TransferEngine::init(const char *server_name, const char *connectable_name, uint64_t rpc_port)
    {
        local_server_name_ = server_name;
        // TODO: write to meta server
        return 0;
    }

    int TransferEngine::freeEngine()
    {
        while (!installed_transports_.empty())
        {
            if (uninstallTransport(installed_transports_.back()->getName()) < 0)
            {
                return -1;
            }
        }
        return 0;
    }

    Transport *TransferEngine::installOrGetTransport(const char *proto, void **args)
    {
        Transport *xport = initTransport(proto);
        if (!xport)
        {
            errno = ENOMEM;
            return NULL;
        }

        if (xport->install(local_server_name_, metadata_, args) < 0)
        {
            goto fail;
        }
        installed_transports_.emplace_back(xport);
        for (const auto& mem: local_memory_regions_) {
            if (xport->registerLocalMemory(mem.addr, mem.length, mem.location, mem.remote_accessible) < 0) {
                goto fail;
            }
        }
        return xport;
    fail:
        delete xport;
        return NULL;
    }

    int TransferEngine::uninstallTransport(const char *proto)
    {
        for (auto it = installed_transports_.begin(); it != installed_transports_.end(); ++it)
        {
            if (strcmp((*it)->getName(), proto) == 0)
            {
                delete *it;
                installed_transports_.erase(it);
                return 0;
            }
        }
        errno = EINVAL;
        return -1;
    }

    Transport::SegmentHandle TransferEngine::openSegment(const char *segment_name)
    {
// return metadata_->getSegmentDesc(segment_name);
#ifdef USE_LOCAL_DESC
        return 0;
#else
        return metadata_->getSegmentID(segment_name);
#endif
    }

    int TransferEngine::closeSegment(Transport::SegmentHandle seg_id)
    {
        // TODO
        return 0;
    }

    int TransferEngine::registerLocalMemory(void *addr, size_t length, const std::string &location, bool update_metadata)
    {
        for (auto& local_memory_region: local_memory_regions_) {
            if (overlap(addr, length, local_memory_region.addr, local_memory_region.length)) {
                LOG(ERROR) << "Memory region overlap";
                return -1;
            }
        }
        for (auto &xport : installed_transports_)
        {
            if (xport->registerLocalMemory(addr, length, location, update_metadata) < 0)
            {
                return -1;
            }
        }
        local_memory_regions_.push_back({addr, length, location.c_str(), update_metadata});
        return 0;
    }

    int TransferEngine::unregisterLocalMemory(void *addr, bool update_metadata)
    {
        for (auto it = local_memory_regions_.begin(); it != local_memory_regions_.end(); ++it) {
            if (it->addr == addr) {
                for (auto &xport : installed_transports_)
                {
                    if (xport->unregisterLocalMemory(addr, update_metadata) < 0)
                    {
                        return -1;
                    }
                }
                local_memory_regions_.erase(it);
                break;
             }
         }
        return 0;
    }

    int TransferEngine::registerLocalMemoryBatch(const std::vector<BufferEntry> &buffer_list, const std::string &location)
    {
        for (auto &xport : installed_transports_)
        {
            if (xport->registerLocalMemoryBatch(buffer_list, location) < 0)
            {
                return -1;
            }
        }
        return 0;
    }

    int TransferEngine::unregisterLocalMemoryBatch(const std::vector<void *> &addr_list)
    {
        for (auto &xport : installed_transports_)
        {
            if (xport->unregisterLocalMemoryBatch(addr_list) < 0)
            {
                return -1;
            }
        }
        return 0;
    }

    Transport *TransferEngine::findName(const char *name, size_t n)
    {
        for (const auto &xport : installed_transports_)
        {
            if (strncmp(xport->getName(), name, n) == 0)
            {
                return xport;
            }
        }
        return NULL;
    }

    Transport *TransferEngine::initTransport(const char *proto)
    {
        if (std::string(proto) == "rdma")
        {
            return new RdmaTransport();
        }
#ifdef USE_CUDA
        else if (std::string(proto) == "nvmeof")
        {
            return new NVMeoFTransport();
        }
#endif
        else
        {
            LOG(ERROR) << "Unsupported Transport Protocol: " << proto;
            return NULL;
        }
    }
}
