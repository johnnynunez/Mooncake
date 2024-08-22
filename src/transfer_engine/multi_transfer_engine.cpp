#include "multi_transfer_engine.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transport.h"

namespace mooncake
{
    int TransferEnginev2::init(const char *server_name, const char * connectable_name, uint64_t rpc_port) {
        // TODO: write to meta server

        return 0;
    }

    int TransferEnginev2::freeEngine() {
        while (!installed_transports_.empty())
        {
            if (uninstallTransport(installed_transports_.back()->getName()) < 0)
            {
                return -1;
            }
        }
        return 0;
    }

    Transport *TransferEnginev2::installOrGetTransport(const char* proto, void **args)
    {
        Transport *xport = initTransport(proto);
        if (!xport)
        {
            errno = ENOMEM;
            return NULL;
        }

        if (xport->install(local_server_name_, meta_, args) < 0)
        {
            goto fail;
        }
        installed_transports_.emplace_back(xport);
        return xport;
    fail:
        delete xport;
        return NULL;
    }

    int TransferEnginev2::uninstallTransport(const char* proto)
    {
        for (auto it = installed_transports_.begin(); it != installed_transports_.end(); ++it)
        {
            if ((*it)->getName() == proto)
            {
                delete *it;
                installed_transports_.erase(it);
                return 0;
            }
        }
        errno = EINVAL;
        return -1;
    }

    Transport::SegmentHandle TransferEnginev2::openSegment(const char *segment_name)
    {
        // return meta_->getSegmentDesc(segment_name);
        #ifdef USE_LOCAL_DESC
        return 0;
        #else
        return meta_->getSegmentID(segment_name);
        #endif
    }

    int TransferEnginev2::closeSegment(Transport::SegmentHandle seg_id)
    {
        // TODO
        return 0;
    }

    int TransferEnginev2::registerLocalMemory(void *addr, size_t length, const std::string &location, bool remote_accessible) {
        for (auto& xport: installed_transports_) {
            if (xport->registerLocalMemory(addr, length, location, remote_accessible) < 0) {
                return -1;
            }
        }
        return 0;
    }

    int TransferEnginev2::unregisterLocalMemory(void *addr) {
        for (auto &xport : installed_transports_)
        {
            if (xport->unregisterLocalMemory(addr) < 0)
            {
                return -1;
            }
        }
        return 0;
    }

    Transport *TransferEnginev2::findName(const char *name, size_t n)
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

    Transport *TransferEnginev2::initTransport(const char *proto)
    {
        if (std::string(proto) == "rdma")
        {
            return new RDMATransport();
        }
        else if (std::string(proto) == "nvmeof")
        {
            return new NVMeoFTransport();
        }
        else
        {
            LOG(ERROR) << "Unsupported Transport Protocol: " << proto;
            return NULL;
        }
    }
}
