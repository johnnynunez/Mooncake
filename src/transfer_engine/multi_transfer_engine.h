#ifndef MULTI_TRANSFER_ENGINE_H_
#define MULTI_TRANSFER_ENGINE_H_

#include <asm-generic/errno-base.h>
#include <limits.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "transfer_engine/transfer_engine.h"
#include "transport.h"

namespace mooncake
{
    struct MultiTransferEngine
    {

    public:
        int freeEngine()
        {
            while (!installed_transports.empty())
            {
                if (uninstallTransport(installed_transports.back()) < 0)
                {
                    return -1;
                }
            }
            return 0;
        }

        Transport *installTransport(const char *proto, const char *name, void **args)
        {
            if (findName(name) != NULL)
            {
                errno = EEXIST;
                return NULL;
            }
            Transport *xport = initTransport(name);
            if (!xport)
            {
                errno = ENOMEM;
                return NULL;
            }

            if (xport->install(args) < 0)
            {
                goto fail;
            }
            installed_transports.emplace_back(xport);
            return xport;
        fail:
            delete xport;
            return NULL;
        }

        int uninstallTransport(Transport *xport)
        {
            for (auto it = installed_transports.begin(); it != installed_transports.end(); ++it)
            {
                if (*it == xport)
                {
                    delete xport;
                    installed_transports.erase(it);
                    return 0;
                }
            }
            errno = EINVAL;
            return -1;
        }

        SegmentID openSegment(const char *path)
        {
            const char *pos = NULL;
            if (path == NULL || (pos = strchr(path, ':')) == NULL)
            {
                errno = EINVAL;
                return -1;
            }

            auto xport = findName(path, pos - path);
            if (!xport)
            {
                errno = ENOENT;
                return -1;
            }

            auto seg_id = xport->openSegment(pos + 1);
            if (seg_id < 0)
            {
                goto fail;
            }
            return seg_id;

        fail:
            return -1;
        }

        int closeSegment(Transport *xport, SegmentID seg_id)
        {
            if (seg_id < 0)
            {
                errno = EINVAL;
                return -1;
            }
            if (xport->closeSegment(seg_id) < 0)
            {
                return -1;
            }
            return 0;
        }

    private:
        Transport *findName(const char *name, size_t n = SIZE_MAX)
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
            if (std::string(proto) == "dummy")
            {
                return new DummyTransport();
            }
        }

        std::vector<Transport *> installed_transports;
    };
}

#endif