// transfer_metadata.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_metadata.h"
#include "transfer_engine/common.h"

#include <set>

namespace mooncake
{

    const static std::string ServerDescPrefix = "mooncake/serverdesc/";

#ifdef MOONCAKE_USE_ETCD
    struct TransferMetadataImpl
    {
        TransferMetadataImpl(const std::string &metadata_uri)
            : client_(metadata_uri) {}

        ~TransferMetadataImpl() {}

        bool get(const std::string &key, Json::Value &value)
        {
            Json::Reader reader;
            uint32_t flags = 0;
            auto resp = client_.get(key);
            if (!resp.is_ok())
            {
                if (resp.error_code() != 100)
                {
                    LOG(ERROR) << "Error from etcd client: " << resp.error_code()
                               << ", message: " << resp.error_message();
                }
                return false;
            }
            auto json_file = resp.value().as_string();
            if (!reader.parse(json_file, value))
                return false;
            LOG(INFO) << "Get ServerDesc, key=" << key << ", value=" << json_file;
            return true;
        }

        bool set(const std::string &key, const Json::Value &value)
        {
            Json::FastWriter writer;
            const std::string json_file = writer.write(value);
            LOG(INFO) << "Put ServerDesc, key=" << key << ", value=" << json_file;
            auto resp = client_.put(key, json_file);
            if (!resp.is_ok())
            {
                LOG(ERROR) << "Error from etcd client: " << resp.error_code()
                           << ", message: " << resp.error_message();
                return false;
            }
            return true;
        }

        bool remove(const std::string &key)
        {
            auto resp = client_.rm(key);
            if (!resp.is_ok())
            {
                if (resp.error_code() != 100)
                {
                    LOG(ERROR) << "Error from etcd client: " << resp.error_code()
                               << ", message: " << resp.error_message();
                }
                return false;
            }
            return true;
        }

        etcd::SyncClient client_;
    };
#else
    struct TransferMetadataImpl
    {
        TransferMetadataImpl(const std::string &metadata_uri)
        {
            const std::string connect_string = "--SERVER=" + metadata_uri;
            client_ = memcached(connect_string.c_str(), connect_string.length());
            if (!client_)
            {
                LOG(ERROR) << "Cannot allocate memcached objects";
                exit(EXIT_FAILURE);
            }
        }

        ~TransferMetadataImpl()
        {
            if (client_)
            {
                memcached_free(client_);
                client_ = nullptr;
            }
        }

        bool get(const std::string &key, Json::Value &value)
        {
            Json::Reader reader;
            uint32_t flags = 0;
            memcached_return_t rc;
            size_t length = 0;
            char *json_file = memcached_get(client_, key.c_str(), key.length(), &length, &flags, &rc);
            if (!json_file || !reader.parse(json_file, json_file + length, value))
                return false;
            LOG(INFO) << "Get ServerDesc, key=" << key << ", value=" << json_file;
            free(json_file);
            return true;
        }

        bool set(const std::string &key, const Json::Value &value)
        {
            Json::FastWriter writer;
            const std::string json_file = writer.write(value);
            LOG(INFO) << "Put ServerDesc, key=" << key << ", value=" << json_file;
            memcached_return_t rc = memcached_set(client_, key.c_str(), key.length(), &json_file[0], json_file.size(), 0, 0);
            return memcached_success(rc);
        }

        bool remove(const std::string &key)
        {
            return memcached_success(memcached_delete(client_, key.c_str(), key.length(), 0));
        }

        memcached_st *client_;
    };
#endif // MOONCAKE_USE_ETCD

    TransferMetadata::TransferMetadata(const std::string &metadata_uri)
        : listener_running_(false)
    {
        impl_ = std::make_shared<TransferMetadataImpl>(metadata_uri);
        if (!impl_)
        {
            LOG(ERROR) << "Cannot allocate TransferMetadataImpl objects";
            exit(EXIT_FAILURE);
        }
    }

    TransferMetadata::~TransferMetadata()
    {
        if (listener_running_)
        {
            listener_running_ = false;
            listener_.join();
        }
    }

    int TransferMetadata::updateSegmentDesc(const std::string &server_name, const SegmentDesc &desc)
    {
        Json::Value serverJSON;
        serverJSON["name"] = desc.name;

        Json::Value devicesJSON(Json::arrayValue);
        for (const auto &device : desc.devices)
        {
            Json::Value deviceJSON;
            deviceJSON["name"] = device.name;
            deviceJSON["lid"] = device.lid;
            deviceJSON["gid"] = device.gid;
            devicesJSON.append(deviceJSON);
        }
        serverJSON["devices"] = devicesJSON;

        Json::Value buffersJSON(Json::arrayValue);
        for (const auto &buffer : desc.buffers)
        {
            Json::Value bufferJSON;
            bufferJSON["name"] = buffer.name;
            bufferJSON["addr"] = static_cast<Json::UInt64>(buffer.addr);
            bufferJSON["length"] = static_cast<Json::UInt64>(buffer.length);
            Json::Value rkeyJSON(Json::arrayValue);
            for (auto &entry : buffer.rkey)
                rkeyJSON.append(entry);
            bufferJSON["rkey"] = rkeyJSON;
            Json::Value lkeyJSON(Json::arrayValue);
            for (auto &entry : buffer.lkey)
                lkeyJSON.append(entry);
            bufferJSON["lkey"] = lkeyJSON;
            buffersJSON.append(bufferJSON);
        }
        serverJSON["buffers"] = buffersJSON;

        Json::Value priorityMatrixJSON;
        for (auto &entry : desc.priority_matrix)
        {
            Json::Value priorityItemJSON(Json::arrayValue);
            Json::Value preferredRnicListJSON(Json::arrayValue);
            for (auto &device_name : entry.second.preferred_rnic_list)
                preferredRnicListJSON.append(device_name);
            priorityItemJSON.append(preferredRnicListJSON);
            Json::Value availableRnicListJSON(Json::arrayValue);
            for (auto &device_name : entry.second.available_rnic_list)
                availableRnicListJSON.append(device_name);
            priorityItemJSON.append(availableRnicListJSON);
            priorityMatrixJSON[entry.first] = priorityItemJSON;
        }
        serverJSON["priority_matrix"] = priorityMatrixJSON;

        if (!impl_->set(ServerDescPrefix + server_name, serverJSON))
        {
            LOG(ERROR) << "Failed to put description of " << server_name;
            return -1;
        }

        return 0;
    }

    int TransferMetadata::removeSegmentDesc(const std::string &server_name)
    {
        if (!impl_->remove(ServerDescPrefix + server_name))
        {
            LOG(ERROR) << "Failed to remove description of " << server_name;
            return -1;
        }
        return 0;
    }

    std::shared_ptr<TransferMetadata::SegmentDesc> TransferMetadata::getSegmentDesc(const std::string &server_name)
    {
        Json::Value serverJSON;
        if (!impl_->get(ServerDescPrefix + server_name, serverJSON))
        {
            LOG(ERROR) << "Failed to get description of " << server_name;
            return nullptr;
        }

        auto desc = std::make_shared<SegmentDesc>();
        desc->name = serverJSON["name"].asString();

        for (const auto &deviceJSON : serverJSON["devices"])
        {
            DeviceDesc device;
            device.name = deviceJSON["name"].asString();
            device.lid = deviceJSON["lid"].asUInt();
            device.gid = deviceJSON["gid"].asString();
            desc->devices.push_back(device);
        }

        for (const auto &bufferJSON : serverJSON["buffers"])
        {
            BufferDesc buffer;
            buffer.name = bufferJSON["name"].asString();
            buffer.addr = bufferJSON["addr"].asUInt64();
            buffer.length = bufferJSON["length"].asUInt64();
            for (const auto &rkeyJSON : bufferJSON["rkey"])
                buffer.rkey.push_back(rkeyJSON.asUInt());
            for (const auto &lkeyJSON : bufferJSON["lkey"])
                buffer.lkey.push_back(lkeyJSON.asUInt());
            desc->buffers.push_back(buffer);
        }

        auto priorityMatrixJSON = serverJSON["priority_matrix"];
        for (const auto &key : priorityMatrixJSON.getMemberNames())
        {
            const Json::Value &value = priorityMatrixJSON[key];
            if (value.isArray() && value.size() == 2)
            {
                PriorityItem item;
                for (const auto &array : value[0])
                {
                    auto device_name = array.asString();
                    item.preferred_rnic_list.push_back(device_name);
                    int device_index = 0;
                    for (auto &entry : desc->devices)
                    {
                        if (entry.name == device_name)
                        {
                            item.preferred_rnic_id_list.push_back(device_index);
                            break;
                        }
                        device_index++;
                    }
                    LOG_ASSERT(device_index != (int)desc->devices.size());
                }
                for (const auto &array : value[1])
                {
                    auto device_name = array.asString();
                    item.available_rnic_list.push_back(device_name);
                    int device_index = 0;
                    for (auto &entry : desc->devices)
                    {
                        if (entry.name == device_name)
                        {
                            item.available_rnic_id_list.push_back(device_index);
                            break;
                        }
                        device_index++;
                    }
                    LOG_ASSERT(device_index != (int)desc->devices.size());
                }
                desc->priority_matrix[key] = item;
            }
        }

        return desc;
    }

    std::string TransferMetadata::encode(const HandShakeDesc &desc)
    {
        Json::Value root;
        root["local_nic_path"] = desc.local_nic_path;
        root["peer_nic_path"] = desc.peer_nic_path;
        Json::Value qpNums(Json::arrayValue);
        for (const auto &qp : desc.qp_num)
            qpNums.append(qp);
        root["qp_num"] = qpNums;
        Json::FastWriter writer;
        auto serialized = writer.write(root);
        LOG(INFO) << "Send Endpoint Handshake Info: " << serialized;
        return serialized;
    }

    int TransferMetadata::decode(const std::string &serialized, HandShakeDesc &desc)
    {
        Json::Value root;
        Json::Reader reader;

        if (serialized.empty() || !reader.parse(serialized, root))
            return -1;

        LOG(INFO) << "Receive Endpoint Handshake Info: " << serialized;
        desc.local_nic_path = root["local_nic_path"].asString();
        desc.peer_nic_path = root["peer_nic_path"].asString();
        for (const auto &qp : root["qp_num"])
            desc.qp_num.push_back(qp.asUInt());

        return 0;
    }

    int TransferMetadata::startHandshakeDaemon(OnReceiveHandShake on_receive_handshake, uint16_t listen_port)
    {
        sockaddr_in bind_address;
        int on = 1, listen_fd = -1;
        memset(&bind_address, 0, sizeof(sockaddr_in));
        bind_address.sin_family = AF_INET;
        bind_address.sin_port = htons(listen_port);
        bind_address.sin_addr.s_addr = INADDR_ANY;

        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0)
        {
            PLOG(ERROR) << "Failed to create socket";
            return -1;
        }

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        {
            PLOG(ERROR) << "Failed to set socket timeout";
            close(listen_fd);
            return -1;
        }

        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        {
            PLOG(ERROR) << "Failed to set address reusable";
            close(listen_fd);
            return -1;
        }

        if (bind(listen_fd, (sockaddr *)&bind_address, sizeof(sockaddr_in)) < 0)
        {
            PLOG(ERROR) << "Failed to bind address";
            close(listen_fd);
            return -1;
        }

        if (listen(listen_fd, 5))
        {
            PLOG(ERROR) << "Failed to listen";
            close(listen_fd);
            return -1;
        }

        listener_running_ = true;
        listener_ = std::thread(
            [this, listen_fd, on_receive_handshake]()
            {
                while (listener_running_)
                {
                    sockaddr_in addr;
                    socklen_t addr_len = sizeof(sockaddr_in);
                    int conn_fd = accept(listen_fd, (sockaddr *)&addr, &addr_len);
                    if (conn_fd < 0)
                    {
                        if (errno != EWOULDBLOCK)
                            PLOG(ERROR) << "Failed to accept socket connection";
                        continue;
                    }

                    if (addr.sin_family != AF_INET)
                    {
                        LOG(ERROR) << "Unsupported socket type: " << addr.sin_family;
                        close(conn_fd);
                        continue;
                    }

                    char inet_str[INET_ADDRSTRLEN];
                    if (!inet_ntop(AF_INET, &(addr.sin_addr), inet_str, INET_ADDRSTRLEN))
                    {
                        PLOG(ERROR) << "Failed to parse incoming address";
                        close(conn_fd);
                        continue;
                    }

                    struct timeval timeout;
                    timeout.tv_sec = 60;
                    timeout.tv_usec = 0;
                    if (setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
                    {
                        PLOG(ERROR) << "Failed to set socket timeout";
                        close(conn_fd);
                        continue;
                    }

                    LOG(INFO) << "New connection: " << inet_str << ":" << ntohs(addr.sin_port) << ", fd: " << conn_fd;

                    HandShakeDesc local_desc, peer_desc;
                    int ret = decode(readString(conn_fd), peer_desc);
                    if (ret)
                    {
                        PLOG(ERROR) << "Failed to receive and decode remote handshake descriptor";
                        close(conn_fd);
                        continue;
                    }

                    ret = on_receive_handshake(peer_desc, local_desc);
                    if (ret)
                    {
                        PLOG(ERROR) << "Failed to perform QP handshake";
                        close(conn_fd);
                        continue;
                    }

                    ret = writeString(conn_fd, encode(local_desc));
                    if (ret)
                    {
                        PLOG(ERROR) << "Failed to encode and send local handshake descriptor";
                        close(conn_fd);
                        continue;
                    }

                    close(conn_fd);
                }
                return;
            });

        return 0;
    }

    int TransferMetadata::sendHandshake(const std::string &peer_server_name,
                                        const HandShakeDesc &local_desc,
                                        HandShakeDesc &peer_desc)
    {
        int conn_fd = -1;
        int on = 1;
        struct addrinfo hints;
        struct addrinfo *result, *rp;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        std::string hostname = peer_server_name;
        uint16_t port = kDefaultServerPort;
        auto pos = peer_server_name.find(':');
        if (pos != peer_server_name.npos)
        {
            hostname = peer_server_name.substr(0, pos);
            auto port_str = peer_server_name.substr(pos + 1);
            int val = std::atoi(port_str.c_str());
            if (val <= 0 || val > 65535)
                PLOG(ERROR) << "Illegal port number in " << peer_server_name;
            else
                port = (uint16_t)port;
        }

        char service[16];
        sprintf(service, "%u", port);
        if (getaddrinfo(hostname.c_str(), service, &hints, &result))
        {
            PLOG(ERROR) << "Failed to get address from " << peer_server_name;
            return -1;
        }

        for (rp = result; conn_fd < 0 && rp; rp = rp->ai_next)
        {
            conn_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (conn_fd == -1)
            {
                LOG(ERROR) << "Failed to create socket";
                continue;
            }
            if (setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
            {
                PLOG(ERROR) << "Failed to set address reusable";
                close(conn_fd);
                conn_fd = -1;
                continue;
            }
            if (connect(conn_fd, rp->ai_addr, rp->ai_addrlen))
            {
                PLOG(ERROR) << "Failed to connect " << peer_server_name;
                close(conn_fd);
                conn_fd = -1;
                continue;
            }
        }

        freeaddrinfo(result);
        if (conn_fd < 0)
            return -1;

        struct timeval timeout;
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;
        if (setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        {
            PLOG(ERROR) << "Failed to set socket timeout";
            close(conn_fd);
            return -1;
        }

        int ret = writeString(conn_fd, encode(local_desc));
        if (ret)
        {
            PLOG(ERROR) << "Failed to encode and send local handshake descriptor to " << peer_server_name;
            close(conn_fd);
            return ret;
        }

        ret = decode(readString(conn_fd), peer_desc);
        if (ret)
        {
            PLOG(ERROR) << "Failed to receive and decode remote handshake descriptor from " << peer_server_name;
            close(conn_fd);
            return ret;
        }

        close(conn_fd);
        return 0;
    }

    int TransferMetadata::parseNicPriorityMatrix(const std::string &nic_priority_matrix,
                                                 PriorityMatrix &priority_map,
                                                 std::vector<std::string> &rnic_list)
    {
        std::set<std::string> rnic_set;
        Json::Value root;
        Json::Reader reader;

        if (nic_priority_matrix.empty() || !reader.parse(nic_priority_matrix, root))
        {
            LOG(ERROR) << "JSON parsing failed";
            return -1;
        }

        // 检查根节点是否为对象
        if (!root.isObject())
        {
            LOG(ERROR) << "JSON root is not an object";
            return -1;
        }

        // 遍历 JSON 对象
        priority_map.clear();
        for (const auto &key : root.getMemberNames())
        {
            const Json::Value &value = root[key];
            if (value.isArray() && value.size() == 2)
            {
                PriorityItem item;
                for (const auto &array : value[0])
                {
                    auto device_name = array.asString();
                    item.preferred_rnic_list.push_back(device_name);
                    auto iter = rnic_set.find(device_name);
                    if (iter == rnic_set.end())
                    {
                        item.preferred_rnic_id_list.push_back(rnic_set.size());
                        rnic_set.insert(device_name);
                    }
                    else
                    {
                        item.preferred_rnic_id_list.push_back(std::distance(rnic_set.begin(), iter));
                    }
                }
                for (const auto &array : value[1])
                {
                    auto device_name = array.asString();
                    item.available_rnic_list.push_back(device_name);
                    auto iter = rnic_set.find(device_name);
                    if (iter == rnic_set.end())
                    {
                        item.available_rnic_id_list.push_back(rnic_set.size());
                        rnic_set.insert(device_name);
                    }
                    else
                    {
                        item.available_rnic_id_list.push_back(std::distance(rnic_set.begin(), iter));
                    }
                }
                priority_map[key] = item;
            }
            else
            {
                LOG(ERROR) << "size " << value.size() << " " << value.isArray();
                LOG(ERROR) << "Invalid array structure in JSON";
                return -1;
            }
        }

        rnic_list.clear();
        for (auto &entry : rnic_set)
            rnic_list.push_back(entry);

        return 0;
    }

}