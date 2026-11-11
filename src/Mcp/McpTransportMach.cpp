/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "McpTransport.h"

#include <bsm/libbsm.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace InGameOverlay {

namespace {

constexpr mach_msg_id_t ConnectMessageId = 0x494F0001;
constexpr mach_msg_id_t ConnectedMessageId = 0x494F0002;
constexpr mach_msg_id_t DataMessageId = 0x494F0003;
constexpr mach_msg_size_t MaxChunkSize = 16 * 1024;
// Mach ports can't be waited on with a wake up source without a port set, polling is enough here.
constexpr mach_msg_timeout_t PollTimeoutMs = 100;

struct Message_t
{
    mach_msg_header_t Header;
    mach_msg_size_t Size;
    char Data[MaxChunkSize];
};

struct ReceivedMessage_t
{
    Message_t Message;
    // The kernel writes the trailer right after the received message, so not at a fixed offset. This only reserves the room for it.
    mach_msg_audit_trailer_t TrailerRoom;
};

// com.nemirtingas.ingame_overlay.mcp.<pid> or com.nemirtingas.ingame_overlay.mcp.<pid>.<name>
bool GetServiceName(McpEndpoint_t const& endpoint, name_t name)
{
    if (!McpIsValidEndpointName(endpoint.Name))
        return false;

    if (endpoint.Name.empty())
        snprintf(name, sizeof(name_t), "com.nemirtingas.ingame_overlay.mcp.%u", endpoint.Pid);
    else
        snprintf(name, sizeof(name_t), "com.nemirtingas.ingame_overlay.mcp.%u.%s", endpoint.Pid, endpoint.Name.c_str());
    return true;
}

mach_port_t CreateReceivePort()
{
    mach_port_t port = MACH_PORT_NULL;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS)
        return MACH_PORT_NULL;

    return port;
}

kern_return_t ReceiveMessage(mach_port_t port, ReceivedMessage_t& received, mach_msg_timeout_t timeout)
{
    return mach_msg(
        &received.Message.Header,
        MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
        0,
        sizeof(received),
        port,
        timeout,
        MACH_PORT_NULL);
}

// The bootstrap namespace is shared with every process in the session: only talk to our own user.
bool IsFromCurrentUser(ReceivedMessage_t const& received)
{
    auto const& header = received.Message.Header;
    auto trailer = reinterpret_cast<mach_msg_audit_trailer_t const*>(reinterpret_cast<char const*>(&header) + round_msg(header.msgh_size));
    return audit_token_to_euid(trailer->msgh_audit) == geteuid();
}

bool IsPortAlive(mach_port_t port)
{
    mach_port_type_t type = 0;
    return mach_port_type(mach_task_self(), port, &type) == KERN_SUCCESS && (type & MACH_PORT_TYPE_DEAD_NAME) == 0;
}

class MachConnection_t : public McpConnection_t
{
    mach_port_t _ReceivePort;
    mach_port_t _SendPort;
    std::shared_ptr<std::atomic<bool>> _Stop;
    std::string _Pending;

public:
    MachConnection_t(mach_port_t receivePort, mach_port_t sendPort, std::shared_ptr<std::atomic<bool>> stop):
        _ReceivePort(receivePort),
        _SendPort(sendPort),
        _Stop(std::move(stop))
    {
    }

    ~MachConnection_t() override
    {
        mach_port_deallocate(mach_task_self(), _SendPort);
        mach_port_mod_refs(mach_task_self(), _ReceivePort, MACH_PORT_RIGHT_RECEIVE, -1);
    }

    size_t Read(void* buffer, size_t size) override
    {
        while (_Pending.empty())
        {
            if ((_Stop != nullptr && _Stop->load()) || !IsPortAlive(_SendPort))
                return 0;

            ReceivedMessage_t received;
            const kern_return_t res = ReceiveMessage(_ReceivePort, received, PollTimeoutMs);
            if (res == MACH_RCV_TIMED_OUT)
                continue;

            if (res != MACH_MSG_SUCCESS)
                return 0;

            if (received.Message.Header.msgh_id == DataMessageId && IsFromCurrentUser(received))
                _Pending.assign(received.Message.Data, std::min(received.Message.Size, MaxChunkSize));

            mach_msg_destroy(&received.Message.Header);
        }

        const size_t count = std::min(size, _Pending.size());
        memcpy(buffer, _Pending.data(), count);
        _Pending.erase(0, count);
        return count;
    }

    bool Write(void const* buffer, size_t size) override
    {
        auto cur = static_cast<char const*>(buffer);
        while (size != 0)
        {
            Message_t message;
            const mach_msg_size_t chunk = static_cast<mach_msg_size_t>(std::min<size_t>(size, MaxChunkSize));
            memset(&message.Header, 0, sizeof(message.Header));
            message.Header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
            message.Header.msgh_size = static_cast<mach_msg_size_t>(offsetof(Message_t, Data) + ((chunk + 3u) & ~3u));
            message.Header.msgh_remote_port = _SendPort;
            message.Header.msgh_id = DataMessageId;
            message.Size = chunk;
            memcpy(message.Data, cur, chunk);

            if (mach_msg(&message.Header, MACH_SEND_MSG, message.Header.msgh_size, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != MACH_MSG_SUCCESS)
                return false;

            cur += chunk;
            size -= chunk;
        }
        return true;
    }
};

class MachListener_t : public McpListener_t
{
    mach_port_t _ServicePort;
    std::shared_ptr<std::atomic<bool>> _Stop;

public:
    explicit MachListener_t(mach_port_t servicePort):
        _ServicePort(servicePort),
        _Stop(std::make_shared<std::atomic<bool>>(false))
    {
    }

    ~MachListener_t() override
    {
        // Destroying the receive right is what removes the name from the bootstrap server.
        mach_port_mod_refs(mach_task_self(), _ServicePort, MACH_PORT_RIGHT_RECEIVE, -1);
    }

    std::unique_ptr<McpConnection_t> Accept() override
    {
        while (!_Stop->load())
        {
            ReceivedMessage_t received;
            const kern_return_t res = ReceiveMessage(_ServicePort, received, PollTimeoutMs);
            if (res == MACH_RCV_TIMED_OUT)
                continue;

            if (res != MACH_MSG_SUCCESS)
                return nullptr;

            if (received.Message.Header.msgh_id != ConnectMessageId || !IsFromCurrentUser(received) || received.Message.Header.msgh_remote_port == MACH_PORT_NULL)
            {
                mach_msg_destroy(&received.Message.Header);
                continue;
            }

            const mach_port_t clientPort = received.Message.Header.msgh_remote_port;
            const mach_port_t receivePort = CreateReceivePort();
            if (receivePort == MACH_PORT_NULL)
            {
                mach_msg_destroy(&received.Message.Header);
                continue;
            }

            mach_msg_header_t reply{};
            reply.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
            reply.msgh_size = sizeof(reply);
            reply.msgh_remote_port = clientPort;
            reply.msgh_local_port = receivePort;
            reply.msgh_id = ConnectedMessageId;
            if (mach_msg(&reply, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != MACH_MSG_SUCCESS)
            {
                mach_port_mod_refs(mach_task_self(), receivePort, MACH_PORT_RIGHT_RECEIVE, -1);
                mach_msg_destroy(&received.Message.Header);
                continue;
            }

            return std::make_unique<MachConnection_t>(receivePort, clientPort, _Stop);
        }
        return nullptr;
    }

    void Wake() override
    {
        _Stop->store(true);
    }
};

}

std::unique_ptr<McpListener_t> McpListen(McpEndpoint_t const& endpoint)
{
    name_t name;
    if (!GetServiceName(endpoint, name))
        return nullptr;

    const mach_port_t port = CreateReceivePort();
    if (port == MACH_PORT_NULL)
        return nullptr;

    if (mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS)
    {
        // Deprecated in favor of launchd plists, which an injected library doesn't have.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const kern_return_t res = bootstrap_register(bootstrap_port, name, port);
#pragma clang diagnostic pop
        if (res == KERN_SUCCESS)
            return std::make_unique<MachListener_t>(port);

        mach_port_deallocate(mach_task_self(), port);
    }

    mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
    return nullptr;
}

std::unique_ptr<McpConnection_t> McpConnect(McpEndpoint_t const& endpoint)
{
    name_t name;
    if (!GetServiceName(endpoint, name))
        return nullptr;

    mach_port_t servicePort = MACH_PORT_NULL;
    if (bootstrap_look_up(bootstrap_port, name, &servicePort) != KERN_SUCCESS)
        return nullptr;

    const mach_port_t receivePort = CreateReceivePort();
    if (receivePort == MACH_PORT_NULL)
    {
        mach_port_deallocate(mach_task_self(), servicePort);
        return nullptr;
    }

    mach_msg_header_t connect{};
    connect.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
    connect.msgh_size = sizeof(connect);
    connect.msgh_remote_port = servicePort;
    connect.msgh_local_port = receivePort;
    connect.msgh_id = ConnectMessageId;
    kern_return_t res = mach_msg(&connect, MACH_SEND_MSG, sizeof(connect), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    mach_port_deallocate(mach_task_self(), servicePort);

    if (res == MACH_MSG_SUCCESS)
    {
        ReceivedMessage_t received;
        res = ReceiveMessage(receivePort, received, 5000);
        if (res == MACH_MSG_SUCCESS)
        {
            if (received.Message.Header.msgh_id == ConnectedMessageId && IsFromCurrentUser(received) && received.Message.Header.msgh_remote_port != MACH_PORT_NULL)
                return std::make_unique<MachConnection_t>(receivePort, received.Message.Header.msgh_remote_port, nullptr);

            mach_msg_destroy(&received.Message.Header);
        }
    }

    mach_port_mod_refs(mach_task_self(), receivePort, MACH_PORT_RIGHT_RECEIVE, -1);
    return nullptr;
}

std::vector<McpEndpoint_t> McpFindServers()
{
    return {};
}

}
