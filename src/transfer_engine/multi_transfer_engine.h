#ifndef MULTI_TRANSFER_ENGINE_H_
#define MULTI_TRANSFER_ENGINE_H_

#include <asm-generic/errno-base.h>
#include <cstddef>
#include <cstdint>
#include <limits.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rdma_transport.h"
#include "nvmeof_transport.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transfer_metadata.h"
#include "transport.h"

namespace mooncake
{
    struct TransferEnginev2
    {
        struct SegmentDesc
        {
            int type; // RDMA / NVMeoF
            union
            {
                struct
                {
                    void *addr;
                    uint64_t size;
                    const char *location;
                } rdma;
                struct
                {
                    const char *file_path;
                    const char *subsystem_name;
                    const char *proto;
                    const char *ip;
                    uint64_t port;
                    // maybe more needed for mount
                } nvmeof;
            } desc_;
        };

    public:
        TransferEnginev2(std::shared_ptr<TransferMetadata> meta)
            : meta(meta) {}

        int init(const char *name)
        {
            // TODO
            local_name = name;
            return 0;
        }

        int freeEngine()
        {
            // while (!installed_transports.empty())
            // {
            //     if (uninstallTransport(installed_transports.back()) < 0)
            //     {
            //         return -1;
            //     }
            // }
            return 0;
        }

        Transport *installOrGetTransport(const std::string &proto, void **args)
        {
            // TODO: dedpulicaete
            Transport *xport = initTransport(proto.c_str());
            if (!xport)
            {
                errno = ENOMEM;
                return NULL;
            }

            if (xport->install(local_name, meta, args) < 0)
            {
                goto fail;
            }
            installed_transports.emplace_back(xport);
            return xport;
        fail:
            delete xport;
            return NULL;
        }

        int uninstallTransport(const std::string &proto)
        {
            return 0;
            // for (auto it = installed_transports.begin(); it != installed_transports.end(); ++it)
            // {
            //     if (*it == xport)
            //     {
            //         delete xport;
            //         installed_transports.erase(it);
            //         return 0;
            //     }
            // }
            // errno = EINVAL;
            // return -1;
        }

        Transport::SegmentHandle openSegment(const char *path)
        {
            return 1;
            //     const char *pos = NULL;
            //     if (path == NULL || (pos = strchr(path, ':')) == NULL)
            //     {
            //         errno = EINVAL;
            //         return std::make_pair(-1, nullptr);
            //     }

            //     auto xport = findName(path, pos - path);
            //     if (!xport)
            //     {
            //         errno = ENOENT;
            //         return std::make_pair(-1, nullptr);
            //     }

            //     auto seg_id = xport->openSegment(pos + 1);
            //     if (seg_id < 0)
            //     {
            //         goto fail;
            //     }
            //     return std::make_pair(seg_id, xport);

            // fail:
            //     return std::make_pair(-1, nullptr);
        }

        int closeSegment(SegmentID seg_id)
        {
            if (seg_id < 0)
            {
                errno = EINVAL;
                return -1;
            }
            // TODO: get xport
            // if (xport->closeSegment(seg_id) < 0)
            // {
            //     return -1;
            // }
            return 0;
        }

        int registerLocalMemory(void *addr, size_t length, const std::string &location, bool remote_accessible = false)
        {
            return 0;
        }

        int unregisterLocalMemory(void *addr, bool remote_accessible = false)
        {
            return 0;
        }

    private:
        Transport *
        findName(const char *name, size_t n = SIZE_MAX)
        {
            for (const auto &xport : installed_transports)
            {
                if (strncmp(xport->getName(), name, n) == 0)
                {
                    return xport;
                }
            }
            return NULL;
        }

        Transport *initTransport(const char *proto)
        {
            // TODO: return a transport object according to protocol
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

        std::vector<Transport *> installed_transports;
        string local_name;
        std::shared_ptr<TransferMetadata> meta;
    };
}

#endif