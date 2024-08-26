#ifndef ENDPOINT_STORE_H_
#define ENDPOINT_STORE_H_

#include "rdma_endpoint.h"
#include "transfer_engine/rdma_context.h"
#include <atomic>
#include <infiniband/verbs.h>
#include <memory>
#include <optional>

using namespace mooncake;

namespace mooncake
{
    // TODO: this can be implemented in std::concept from c++20
    /* TODO: A better abstraction may be used to reduce redundant codes,
    for example, make "cache eviction policy" a abstract class. Currently,
    cache data structure and eviction policy are put in the same class, for
    different eviction policy may need different data structure
    (e.g. lock-free queue for FIFO) for better performance
    */
    class EndpointStore
    {
    public:
        virtual std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path) = 0;
        virtual std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext *context) = 0;
        virtual int deleteEndpoint(std::string peer_nic_path) = 0;
        virtual void evictEndpoint() = 0;
        virtual size_t getSize() = 0;

        virtual int destroyQPs() = 0;
    };

    // FIFO
    class FIFOEndpointStore : public EndpointStore
    {
    public:
        FIFOEndpointStore(size_t max_size) : max_size_(max_size) {}
        std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
        std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext *context);
        int deleteEndpoint(std::string peer_nic_path);
        void evictEndpoint();
        size_t getSize();

        int destroyQPs();

    private:
        RWSpinlock endpoint_map_lock_;
        std::unordered_map<std::string, std::shared_ptr<RdmaEndPoint>> endpoint_map_;
        std::unordered_map<std::string, std::list<std::string>::iterator> fifo_map_;
        std::list<std::string> fifo_list_;

        size_t max_size_;
    };

    // NSDI 24, similar to clock with quick demotion
    class SIEVEEndpointStore : public EndpointStore
    {
    public:
        SIEVEEndpointStore(size_t max_size) : max_size_(max_size){};
        std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
        std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext *context);
        int deleteEndpoint(std::string peer_nic_path);
        void evictEndpoint();
        size_t getSize();

        int destroyQPs();

    private:
        RWSpinlock endpoint_map_lock_;
        // The bool represents visited
        std::unordered_map<std::string, std::pair<std::shared_ptr<RdmaEndPoint>, std::atomic_bool>> endpoint_map_;
        std::unordered_map<std::string, std::list<std::string>::iterator> fifo_map_;
        std::list<std::string> fifo_list_;

        std::optional<std::list<std::string>::iterator> hand_;

        size_t max_size_;
    };
}

#endif