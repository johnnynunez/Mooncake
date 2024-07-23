// transfer_metadata.cpp
// Copyright (C) 2024 Feng Ren

#include "net/common.h"
#include "net/transfer_metadata.h"

#include <set>
#include <libmemcached/memcached.hpp>

const static std::string ServerDescPrefix = "mooncake/serverdesc/";

struct TransferMetadataImpl
{
    TransferMetadataImpl(const std::string &metadata_uri)
    {
        const std::string connect_string = "--SERVER=" + metadata_uri;
        client_ = memcached(connect_string.c_str(), connect_string.length());
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
        LOG(INFO) << "GET key=" << key << ", value=" << json_file;
        free(json_file);
        return true;
    }

    bool set(const std::string &key, const Json::Value &value)
    {
        Json::FastWriter writer;
        const std::string json_file = writer.write(value);
        LOG(INFO) << "SET key=" << key << ", value=" << json_file;
        memcached_return_t rc = memcached_set(client_, key.c_str(), key.length(), &json_file[0], json_file.size(), 0, 0);
        return memcached_success(rc);
    }

    bool remove(const std::string &key)
    {
        return memcached_success(memcached_delete(client_, key.c_str(), key.length(), 0));
    }

    memcached_st *client_;
};

TransferMetadata::TransferMetadata(const std::string &metadata_uri)
    : listener_running_(false)
{
    impl_ = std::make_shared<TransferMetadataImpl>(metadata_uri);
}

TransferMetadata::~TransferMetadata()
{
    if (listener_running_)
    {
        listener_running_ = false;
        listener_.join();
    }
}

int TransferMetadata::broadcast_server_desc(const std::string &server_name, const ServerDesc &desc)
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

    Json::Value segmentsJSON(Json::arrayValue);
    for (const auto &segment : desc.segments)
    {
        Json::Value segmentJSON;
        segmentJSON["name"] = segment.name;
        segmentJSON["addr"] = static_cast<Json::UInt64>(segment.addr);
        segmentJSON["length"] = static_cast<Json::UInt64>(segment.length);
        Json::Value rkeyJSON(Json::arrayValue);
        for (auto &entry : segment.rkey)
            rkeyJSON.append(entry);
        segmentJSON["rkey"] = rkeyJSON;
        Json::Value preferredRNICJSON(Json::arrayValue);
        for (auto &entry : segment.preferred_rnic)
            preferredRNICJSON.append(entry);
        segmentJSON["preferred_rnic"] = preferredRNICJSON;
        segmentsJSON.append(segmentJSON);
    }
    serverJSON["segments"] = segmentsJSON;

    if (!impl_->set(ServerDescPrefix + server_name, serverJSON))
    {
        LOG(ERROR) << "Failed to put description of " << server_name;
        return -1;
    }

    return 0;
}

int TransferMetadata::remove_server_desc(const std::string &server_name)
{
    int ret = 0;
    if (!impl_->remove(ServerDescPrefix + server_name))
    {
        LOG(ERROR) << "Failed to remove description of " << server_name;
        ret = -1;
    }
    RWSpinlock::WriteGuard guard(server_desc_lock_);
    server_desc_map_.erase(server_name);
    return ret;
}

std::shared_ptr<TransferMetadata::ServerDesc> TransferMetadata::get_server_desc(const std::string &server_name)
{
    {
        RWSpinlock::ReadGuard guard(server_desc_lock_);
        if (server_desc_map_.count(server_name))
            return server_desc_map_[server_name];
    }

    Json::Value serverJSON;
    if (!impl_->get(ServerDescPrefix + server_name, serverJSON))
    {
        LOG(ERROR) << "Failed to get description of " << server_name;
        return nullptr;
    }

    auto desc = std::make_shared<ServerDesc>();
    desc->name = serverJSON["name"].asString();

    for (const auto &deviceJSON : serverJSON["devices"])
    {
        DeviceDesc device;
        device.name = deviceJSON["name"].asString();
        device.lid = deviceJSON["lid"].asUInt();
        device.gid = deviceJSON["gid"].asString();
        desc->devices.push_back(device);
    }

    for (const auto &segmentJSON : serverJSON["segments"])
    {
        SegmentDesc segment;
        segment.name = segmentJSON["name"].asString();
        segment.addr = segmentJSON["addr"].asUInt64();
        segment.length = segmentJSON["length"].asUInt64();
        for (const auto &rkeyJSON : segmentJSON["rkey"])
            segment.rkey.push_back(rkeyJSON.asUInt());
        for (const auto &preferredRNICJSON : segmentJSON["preferred_rnic"])
            segment.preferred_rnic.push_back(preferredRNICJSON.asUInt());
        desc->segments.push_back(segment);
    }

    RWSpinlock::WriteGuard guard(server_desc_lock_);
    server_desc_map_[server_name] = desc;
    return desc;
}

std::string TransferMetadata::encode(const HandShakeDesc &desc)
{
    Json::Value root;
    Json::Value devices(Json::arrayValue);
    for (const auto &device : desc.devices)
    {
        Json::Value deviceJSON;
        deviceJSON["name"] = device.name;
        Json::Value qpNums(Json::arrayValue);
        for (const auto &qp : device.qp_num)
            qpNums.append(qp);
        deviceJSON["qp_num"] = qpNums;
        devices.append(deviceJSON);
    }
    root["devices"] = devices;
    root["server_name"] = desc.server_name;
    Json::FastWriter writer;

    return writer.write(root);
}

int TransferMetadata::decode(const std::string &ser, HandShakeDesc &desc)
{
    Json::Value root;
    Json::Reader reader;

    if (ser.empty() || !reader.parse(ser, root))
        return -1;

    desc.server_name = root["server_name"].asString();
    for (const auto &deviceJSON : root["devices"])
    {
        HandShakeDescImpl device;
        device.name = deviceJSON["name"].asString();
        for (const auto &qp : deviceJSON["qp_num"])
            device.qp_num.push_back(qp.asUInt());
        desc.devices.push_back(device);
    }

    return 0;
}

int TransferMetadata::start_handshake_daemon(OnReceiveHandShake on_receive_handshake, uint16_t listen_port)
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
                int ret = decode(read_string(conn_fd), peer_desc);
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

                ret = write_string(conn_fd, encode(local_desc));
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

int TransferMetadata::send_handshake(const std::string &peer_server_name,
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

    int ret = write_string(conn_fd, encode(local_desc));
    if (ret)
    {
        PLOG(ERROR) << "Failed to encode and send local handshake descriptor to " << peer_server_name;
        close(conn_fd);
        return ret;
    }

    ret = decode(read_string(conn_fd), peer_desc);
    if (ret)
    {
        PLOG(ERROR) << "Failed to receive and decode remote handshake descriptor from " << peer_server_name;
        close(conn_fd);
        return ret;
    }

    close(conn_fd);
    return 0;
}

int TransferMetadata::parse_nic_priority_matrix(const std::string &nic_priority_matrix,
                                                PriorityMap &priority_map,
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
                item.preferred_rnic_list.push_back(array.asString());
                rnic_set.insert(array.asString());
            }
            for (const auto &array : value[1])
            {
                item.available_rnic_list.push_back(array.asString());
                rnic_set.insert(array.asString());
            }
            priority_map[key] = item;
        }
        else
        {
            LOG(ERROR) << "Invalid array structure in JSON";
            return -1;
        }
    }

    rnic_list.clear();
    for (auto &entry : rnic_set)
        rnic_list.push_back(entry);

    return 0;
}