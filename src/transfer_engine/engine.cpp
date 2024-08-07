#include "engine.h"

#include <asm-generic/errno-base.h>
#include <limits.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "transport.h"

struct TransferEngine
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

    struct transfer_segment *openSegment(const char *path)
    {
        const char *pos = NULL;
        if (path == NULL || (pos = strchr(path, ':')) == NULL)
        {
            errno = EINVAL;
            return NULL;
        }

        auto xport = findName(path, pos - path);
        if (!xport)
        {
            errno = ENOENT;
            return NULL;
        }

        struct transfer_segment *seg = new transfer_segment{
            .xport = xport,
            .context = NULL,
        };  
        if (xport->openSegment(pos + 1, seg) < 0)
        {
            goto fail;
        }
        return seg;

    fail:
        delete seg;
        return NULL;
    }

    int closeSegment(struct transfer_segment *seg)
    {
        if (seg == NULL)
        {
            errno = EINVAL;
            return -1;
        }
        if (seg->xport->closeSegment(seg) < 0)
        {
            return -1;
        }
        delete seg;
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
        if (std::string(proto) == "dummy") {
            return new DummyTransport();
        }
    }

    std::vector<Transport *> installed_transports;
};

extern "C"
{

    transfer_engine *init_transfer_engine() { return new transfer_engine(); }

    int free_transfer_engine(transfer_engine *engine)
    {
        if (engine->freeEngine() < 0)
        {
            return -1;
        }
        delete engine;
        return 0;
    }

    Transport *install_transport(TransferEngine *engine, const char *proto, const char *name, void **args)
    {
        return engine->installTransport(proto, name, args);
    }

    int uninstall_transport(TransferEngine *engine, Transport *xport) { return engine->uninstallTransport(xport); }

    struct transfer_segment *open_segment(TransferEngine *engine, const char *path)
    {
        return engine->openSegment(path);
    }

    int close_segment(struct transfer_segment *segment)
    {
        if (segment->xport->closeSegment(segment) < 0)
        {
            return -1;
        }
        delete segment;
        return 0;
    }

    struct transfer_batch *alloc_transfer_batch(Transport *xport, unsigned max_nr)
    {
        struct transfer_batch *batch = new transfer_batch{
            .xport = xport,
            .max_nr = max_nr,
            .context = NULL,
        };

        if (xport->allocBatch(batch, max_nr) < 0)
        {
            goto fail;
        }
        return batch;

    fail:
        delete batch;
        return NULL;
    }

    int free_transfer_batch(struct transfer_batch *batch)
    {
        if (batch->xport->freeBatch(batch) < 0)
        {
            return -1;
        }
        delete batch;
        return 0;
    }

    int submit_transfers(struct transfer_batch *batch, unsigned nr, struct transfer_request *transfers)
    {
        if (nr > (unsigned)INT_MAX)
        {
            errno = E2BIG;
            return 0;
        }

        for (int i = 0; i < (int)nr; i++)
        {
            transfers[i].__internal.batch = batch;
        }

        return batch->xport->submitTransfers(batch, nr, transfers);
    }

    int get_transfer_status(struct transfer_request *transfer, struct transfer_status *status)
    {
        struct transfer_batch *batch = transfer->__internal.batch;
        return batch->xport->getTransferStatus(transfer, status);
    }
}