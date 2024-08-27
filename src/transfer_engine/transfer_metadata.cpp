// transfer_metadata.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_metadata.h"
#include "transfer_engine/common.h"
#include "transfer_engine/config.h"
#include "transfer_engine/error.h"

#include <arpa/inet.h>
#include <bits/stdint-uintn.h>
#include <cassert>
#include <jsoncpp/json/value.h>
#include <netdb.h>
#include <sys/socket.h>
#include <set>
#include <sys/socket.h>

namespace mooncake
{

    const static std::string ServerDescPrefix = "mooncake/serverdesc/";

    struct TransferMetadataImpl
    {
        TransferMetadataImpl(const std::string &metadata_uri)
            : client_(metadata_uri) {}

        ~TransferMetadataImpl() {}

        bool get(const std::string &key, Json::Value &value)
        {
            Json::Reader reader;
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
            if (globalConfig().verbose)
                LOG(INFO) << "Get ServerDesc, key=" << key << ", value=" << json_file;
            return true;
        }

        bool set(const std::string &key, const Json::Value &value)
        {
            Json::FastWriter writer;
            const std::string json_file = writer.write(value);
            if (globalConfig().verbose)
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

    TransferMetadata::TransferMetadata(const std::string &metadata_uri)
        : listener_running_(false)
    {
        impl_ = std::make_shared<TransferMetadataImpl>(metadata_uri);
        if (!impl_)
        {
            LOG(ERROR) << "Cannot allocate TransferMetadataImpl objects";
            exit(EXIT_FAILURE);
        }
        next_segment_id_.store(0);
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
        serverJSON["protocol"] = desc.protocol;

        if (serverJSON["protocol"] == "rdma") {
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
        } else {
            // For NVMeoF, the transfer engine should not modify the metadata.
            assert(false);
        }

        if (!impl_->set(ServerDescPrefix + server_name, serverJSON))
        {
            LOG(ERROR) << "Failed to put description of " << server_name;
            return ERR_METADATA;
        }

        return 0;
    }

    int TransferMetadata::removeSegmentDesc(const std::string &server_name)
    {
        if (!impl_->remove(ServerDescPrefix + server_name))
        {
            LOG(ERROR) << "Failed to remove description of " << server_name;
            return ERR_METADATA;
        }
        return 0;
    }

    std::shared_ptr<TransferMetadata::SegmentDesc> TransferMetadata::getSegmentDesc(const std::string &server_name)
    {
        Json::Value serverJSON;
        if (!impl_->get(server_name, serverJSON))
        {
            LOG(ERROR) << "Failed to get description of " << server_name;
            return nullptr;
        }

        auto desc = std::make_shared<SegmentDesc>();
        if (!desc)
        {
            LOG(ERROR) << "Failed to allocate SegmentDesc object";
            return nullptr;
        }
        desc->name = serverJSON["name"].asString();
        desc->protocol = serverJSON["protocol"].asString();

        if (desc->protocol == "rdma") {
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
        } else {
            for (const auto &bufferJSON : serverJSON["buffers"])
            {
                NVMeoFBufferDesc buffer;
                buffer.file_path = bufferJSON["file_path"].asString();
                buffer.length = bufferJSON["length"].asUInt64();
                const Json::Value& local_path_map = bufferJSON["local_path_map"];
                for (const auto &key : local_path_map.getMemberNames())
                {
                    buffer.local_path_map[key] = local_path_map[key].asString();
                }
                desc->nvmeof_buffers.push_back(buffer);
            }
        }
        LOG(INFO) << "name " << desc->name << " protocol " << desc->protocol;
        // LOG(INFO) << "devices " << desc->devices.size() << " buffers " << desc->buffers.size();
        for (const auto& nvmebuf : desc->nvmeof_buffers) {
            LOG(INFO) << "file_path " << nvmebuf.file_path << " length " << nvmebuf.length;
            for (const auto& entry : nvmebuf.local_path_map) {
                LOG(INFO) << "local_path_map " << entry.first << " " << entry.second;
            }
        }
        return desc;
    }

    std::shared_ptr<TransferMetadata::SegmentDesc> TransferMetadata::getSegmentDescByName(const std::string &segment_name, bool force_update)
    {
        if (!force_update)
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            auto iter = segment_name_to_id_map_.find(segment_name);
            if (iter != segment_name_to_id_map_.end())
                return segment_id_to_desc_map_[iter->second];
        }

        RWSpinlock::WriteGuard guard(segment_lock_);
        auto iter = segment_name_to_id_map_.find(segment_name);
        SegmentID segment_id;
        if (iter != segment_name_to_id_map_.end())
            segment_id = iter->second;
        else
            segment_id = next_segment_id_.fetch_add(1);
        auto server_desc = this->getSegmentDesc(segment_name);
        if (!server_desc)
            return nullptr;
        segment_id_to_desc_map_[segment_id] = server_desc;
        segment_name_to_id_map_[segment_name] = segment_id;
        return server_desc;
    }

    std::shared_ptr<TransferMetadata::SegmentDesc> TransferMetadata::getSegmentDescByID(SegmentID segment_id, bool force_update)
    {
        LOG(INFO) << "get " << segment_id;
        if (force_update)
        {
            RWSpinlock::WriteGuard guard(segment_lock_);
            if (!segment_id_to_desc_map_.count(segment_id))
                return nullptr;
            auto server_desc = getSegmentDesc(segment_id_to_desc_map_[segment_id]->name);
            if (!server_desc)
                return nullptr;
            segment_id_to_desc_map_[segment_id] = server_desc;
            return segment_id_to_desc_map_[segment_id];
        }
        else
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            if (!segment_id_to_desc_map_.count(segment_id))
                return nullptr;
            return segment_id_to_desc_map_[segment_id];
        }
    }

    TransferMetadata::SegmentID TransferMetadata::getSegmentID(const std::string &segment_name)
    {
        {
            RWSpinlock::ReadGuard guard(segment_lock_);
            if (segment_name_to_id_map_.count(segment_name))
                return segment_name_to_id_map_[segment_name];
        }

        RWSpinlock::WriteGuard guard(segment_lock_);
        if (segment_name_to_id_map_.count(segment_name))
            return segment_name_to_id_map_[segment_name];
        auto server_desc = this->getSegmentDesc(segment_name);
        if (!server_desc)
            return -1;
        SegmentID id = next_segment_id_.fetch_add(1);
        LOG(INFO) << "put " << id;
        segment_id_to_desc_map_[id] = server_desc;
        segment_name_to_id_map_[segment_name] = id;
        return id;
    }

    int TransferMetadata::updateLocalSegmentDesc(uint64_t segment_id)
    {
        RWSpinlock::ReadGuard guard(segment_lock_);
        auto desc = segment_id_to_desc_map_[segment_id];
        return this->updateSegmentDesc(desc->name, *desc);
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
        root["reply_msg"] = desc.reply_msg;
        Json::FastWriter writer;
        auto serialized = writer.write(root);
        if (globalConfig().verbose)
            LOG(INFO) << "Send Endpoint Handshake Info: " << serialized;
        return serialized;
    }

    int TransferMetadata::decode(const std::string &serialized, HandShakeDesc &desc)
    {
        Json::Value root;
        Json::Reader reader;

        if (serialized.empty() || !reader.parse(serialized, root))
            return ERR_MALFORMED_JSON;

        if (globalConfig().verbose)
            LOG(INFO) << "Receive Endpoint Handshake Info: " << serialized;
        desc.local_nic_path = root["local_nic_path"].asString();
        desc.peer_nic_path = root["peer_nic_path"].asString();
        for (const auto &qp : root["qp_num"])
            desc.qp_num.push_back(qp.asUInt());
        desc.reply_msg = root["reply_msg"].asString();

        return 0;
    }

    static inline const std::string toString(struct sockaddr *addr)
    {
        if (addr->sa_family == AF_INET)
        {
            struct sockaddr_in *sock_addr = (struct sockaddr_in *)addr;
            char ip[INET_ADDRSTRLEN];
            if (inet_ntop(addr->sa_family, &(sock_addr->sin_addr), ip, INET_ADDRSTRLEN) != NULL)
                return ip;
        }
        else if (addr->sa_family == AF_INET6)
        {
            struct sockaddr_in6 *sock_addr = (struct sockaddr_in6 *)addr;
            char ip[INET6_ADDRSTRLEN];
            if (inet_ntop(addr->sa_family, &(sock_addr->sin6_addr), ip, INET6_ADDRSTRLEN) != NULL)
                return ip;
        }
        return "<unknown>";
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
            return ERR_SOCKET;
        }

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        {
            PLOG(ERROR) << "Failed to set socket timeout";
            close(listen_fd);
            return ERR_SOCKET;
        }

        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        {
            PLOG(ERROR) << "Failed to set address reusable";
            close(listen_fd);
            return ERR_SOCKET;
        }

        if (bind(listen_fd, (sockaddr *)&bind_address, sizeof(sockaddr_in)) < 0)
        {
            PLOG(ERROR) << "Failed to bind address";
            close(listen_fd);
            return ERR_SOCKET;
        }

        if (listen(listen_fd, 5))
        {
            PLOG(ERROR) << "Failed to listen";
            close(listen_fd);
            return ERR_SOCKET;
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

                    if (addr.sin_family != AF_INET && addr.sin_family != AF_INET6)
                    {
                        LOG(ERROR) << "Unsupported socket type, should be AF_INET or AF_INET6";
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

                    if (globalConfig().verbose)
                        LOG(INFO) << "New connection: "
                                  << toString((struct sockaddr *)&addr)
                                  << ":" << ntohs(addr.sin_port);

                    HandShakeDesc local_desc, peer_desc;
                    int ret = decode(readString(conn_fd), peer_desc);
                    if (ret)
                    {
                        PLOG(ERROR) << "Failed to receive handshake message";
                        close(conn_fd);
                        continue;
                    }

                    on_receive_handshake(peer_desc, local_desc);
                    ret = writeString(conn_fd, encode(local_desc));
                    if (ret)
                    {
                        PLOG(ERROR) << "Failed to send handshake message";
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
                PLOG(WARNING) << "Illegal port number in " << peer_server_name
                              << ". Use default port " << port << " instead";
            else
                port = (uint16_t)port;
        }

        char service[16];
        sprintf(service, "%u", port);
        if (getaddrinfo(hostname.c_str(), service, &hints, &result))
        {
            PLOG(ERROR) << "Failed to get IP address of peer server " << peer_server_name
                        << ", check DNS and /etc/hosts, or use IPv4 address instead";
            return ERR_DNS;
        }

        int ret = 0;
        for (rp = result; rp; rp = rp->ai_next)
        {
            ret = doSendHandshake(rp, local_desc, peer_desc);
            if (ret == 0)
            {
                freeaddrinfo(result);
                return 0;
            }
        }

        freeaddrinfo(result);
        return ret;
    }

    int TransferMetadata::doSendHandshake(struct addrinfo *addr,
                                          const HandShakeDesc &local_desc,
                                          HandShakeDesc &peer_desc)
    {
        if (globalConfig().verbose)
            LOG(INFO) << "Try connecting " << toString(addr->ai_addr);

        int on = 1;
        int conn_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (conn_fd == -1)
        {
            PLOG(ERROR) << "Failed to create socket";
            return ERR_SOCKET;
        }
        if (setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        {
            PLOG(ERROR) << "Failed to set address reusable";
            close(conn_fd);
            return ERR_SOCKET;
        }

        struct timeval timeout;
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;
        if (setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        {
            PLOG(ERROR) << "Failed to set socket timeout";
            close(conn_fd);
            return ERR_SOCKET;
        }

        if (connect(conn_fd, addr->ai_addr, addr->ai_addrlen))
        {
            PLOG(ERROR) << "Failed to connect " << toString(addr->ai_addr);
            close(conn_fd);
            return ERR_SOCKET;
        }

        int ret = writeString(conn_fd, encode(local_desc));
        if (ret)
        {
            LOG(ERROR) << "Failed to send handshake message";
            close(conn_fd);
            return ret;
        }

        ret = decode(readString(conn_fd), peer_desc);
        if (ret)
        {
            LOG(ERROR) << "Failed to receive handshake message";
            close(conn_fd);
            return ret;
        }

        if (!peer_desc.reply_msg.empty())
        {
            LOG(ERROR) << "Handshake request rejected by peer endpoint: " << peer_desc.reply_msg;
            close(conn_fd);
            return ERR_REJECT_HANDSHAKE;
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
            LOG(ERROR) << "Malformed format of NIC priority matrix: illegal JSON format";
            return ERR_MALFORMED_JSON;
        }

        if (!root.isObject())
        {
            LOG(ERROR) << "Malformed format of NIC priority matrix: root is not an object";
            return ERR_MALFORMED_JSON;
        }

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
                LOG(ERROR) << "Malformed format of NIC priority matrix: format error";
                return ERR_MALFORMED_JSON;
            }
        }

        rnic_list.clear();
        for (auto &entry : rnic_set)
            rnic_list.push_back(entry);

        return 0;
    }


}