#ifndef ENDPOINT_STORE_H_
#define ENDPOINT_STORE_H_

#include <infiniband/verbs.h>
#include <memory>
#include <optional>
#include "rdma_endpoint.h"
#include "transfer_engine/rdma_context.h"

using namespace mooncake;

namespace mooncake {
    // TODO: this can be implemented in std::concept from c++20
    class EndpointStore
    {
    public:
        virtual std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path) = 0;
        virtual std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext* context) = 0;
        virtual int deleteEndpoint(std::string peer_nic_path) = 0;
        virtual void evictEndpoint() = 0;
        virtual size_t getSize() = 0;
    };

    class FIFOEndpointStore: public EndpointStore
    {
        public:
            FIFOEndpointStore(size_t max_size): max_size_(max_size) {}
            std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
            std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext* context );
            int deleteEndpoint(std::string peer_nic_path);
            void evictEndpoint();
            size_t getSize();

        private:
            RWSpinlock endpoint_map_lock_;
            std::unordered_map<std::string, std::shared_ptr<RdmaEndPoint>> endpoint_map_;
            std::unordered_map<std::string, std::list<std::string>::iterator> fifo_map_;
            std::list<std::string> fifo_list_;

            size_t max_size_;
    };

    class LRUEndpointStore: public EndpointStore
    {
        public:
            LRUEndpointStore(size_t max_size);
            std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
            std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext* context );
            int deleteEndpoint(std::string peer_nic_path);
            void evictEndpoint();
            size_t getSize();
        private:
            std::vector<std::shared_ptr<RdmaEndPoint>> endpoints;
            size_t max_size;
    };

    class LFUEndpointStore: public EndpointStore
    {
        public:
            LFUEndpointStore(size_t max_size);
            std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
            std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext* context );
            int deleteEndpoint(std::string peer_nic_path);
            void evictEndpoint();
            size_t getSize();
        private:
            std::vector<std::shared_ptr<RdmaEndPoint>> endpoints;
            size_t max_size;
    };

    class SIEVEEndpointStore: public EndpointStore
    {
        public:
            SIEVEEndpointStore(size_t max_size);
            std::shared_ptr<RdmaEndPoint> getEndpoint(std::string peer_nic_path);
            std::shared_ptr<RdmaEndPoint> insertEndpoint(std::string peer_nic_path, RdmaContext* context );
            int deleteEndpoint(std::string peer_nic_path);
            void evictEndpoint();
            size_t getSize();
        private:
            std::vector<std::shared_ptr<RdmaEndPoint>> endpoints;
            size_t max_size;
    };
}

#endif