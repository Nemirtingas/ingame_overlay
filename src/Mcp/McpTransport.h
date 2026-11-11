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

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace InGameOverlay {

constexpr size_t McpMaxEndpointNameLength = 32;

// Where a server listens: its process, and a name to tell apart several overlays in the same process (Steam, Epic, GOG...).
// An empty name is the default endpoint of the process.
struct McpEndpoint_t
{
    uint32_t Pid = 0;
    std::string Name;
};

// The name ends up in a file, pipe or service name: only letters, digits, '-' and '_'.
inline bool McpIsValidEndpointName(std::string const& name)
{
    if (name.size() > McpMaxEndpointNameLength)
        return false;

    for (const char c : name)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!valid)
            return false;
    }
    return true;
}

// "pid" or "pid/name", for messages.
inline std::string McpEndpointToString(McpEndpoint_t const& endpoint)
{
    return std::to_string(endpoint.Pid) + (endpoint.Name.empty() ? std::string() : "/" + endpoint.Name);
}

// A byte stream with the client. Read and Write may run at the same time on different threads.
class McpConnection_t
{
public:
    virtual ~McpConnection_t() {}

    // Returns the byte count, or 0 when the connection is closed or when the listener has been woken up.
    virtual size_t Read(void* buffer, size_t size) = 0;
    virtual bool Write(void const* buffer, size_t size) = 0;
};

class McpListener_t
{
public:
    virtual ~McpListener_t() {}

    // Blocks until a client connects, returns nullptr after Wake.
    virtual std::unique_ptr<McpConnection_t> Accept() = 0;

    // Can be called from any thread: unblocks Accept and the Read of the connections it returned, for good.
    virtual void Wake() = 0;
};

// Implemented once per platform in McpTransportPosix.cpp, McpTransportWindows.cpp and McpTransportMach.cpp.
// The endpoint name must be valid, see McpIsValidEndpointName.
std::unique_ptr<McpListener_t> McpListen(McpEndpoint_t const& endpoint);
std::unique_ptr<McpConnection_t> McpConnect(McpEndpoint_t const& endpoint);

// The endpoints which seem to have a server, it can't be implemented with the bootstrap services and returns nothing on macOS.
std::vector<McpEndpoint_t> McpFindServers();

}
