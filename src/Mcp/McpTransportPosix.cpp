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

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace InGameOverlay {

namespace {

// The private directory is what protects the socket: the permissions of the socket itself aren't honored everywhere.
std::string GetSocketDirectory()
{
    char const* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir != nullptr && runtimeDir[0] == '/')
        return std::string(runtimeDir) + "/ingame_overlay";

    return "/tmp/ingame_overlay-" + std::to_string(static_cast<unsigned long>(geteuid()));
}

// A server listens on it. Non blocking: a server busy with a client may have a full backlog, which also means it is in use.
bool IsSocketInUse(sockaddr_un const& address)
{
    const int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0)
        return false;

    fcntl(probe, F_SETFD, FD_CLOEXEC);
    fcntl(probe, F_SETFL, fcntl(probe, F_GETFL) | O_NONBLOCK);
    const bool inUse = connect(probe, reinterpret_cast<sockaddr const*>(&address), sizeof(address)) == 0 || (errno != ECONNREFUSED && errno != ENOENT);
    close(probe);
    return inUse;
}

// mcp-<pid>.sock or mcp-<pid>-<name>.sock
std::string GetSocketFileName(McpEndpoint_t const& endpoint)
{
    return "mcp-" + std::to_string(endpoint.Pid) + (endpoint.Name.empty() ? std::string() : "-" + endpoint.Name) + ".sock";
}

bool ParseSocketFileName(char const* fileName, McpEndpoint_t& endpoint)
{
    if (strncmp(fileName, "mcp-", 4) != 0)
        return false;

    char* end = nullptr;
    const unsigned long pid = strtoul(fileName + 4, &end, 10);
    if (end == fileName + 4 || pid == 0)
        return false;

    const std::string rest(end);
    const std::string suffix(".sock");
    if (rest.size() < suffix.size() || rest.compare(rest.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;

    endpoint.Pid = static_cast<uint32_t>(pid);
    endpoint.Name.clear();
    if (rest.size() == suffix.size())
        return true;

    if (rest[0] != '-')
        return false;

    endpoint.Name = rest.substr(1, rest.size() - suffix.size() - 1);
    return !endpoint.Name.empty() && McpIsValidEndpointName(endpoint.Name);
}

bool GetSocketAddress(McpEndpoint_t const& endpoint, sockaddr_un& address, std::string& path)
{
    if (!McpIsValidEndpointName(endpoint.Name))
        return false;

    path = GetSocketDirectory() + "/" + GetSocketFileName(endpoint);

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
        return false;

    memcpy(address.sun_path, path.c_str(), path.size());
    return true;
}

// /tmp is shared: refuse a directory created by somebody else, or which others can get into.
bool CreatePrivateDirectory(std::string const& path)
{
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
        return false;

    struct stat infos;
    return lstat(path.c_str(), &infos) == 0
        && S_ISDIR(infos.st_mode)
        && infos.st_uid == geteuid()
        && (infos.st_mode & 0077) == 0;
}

class PosixConnection_t : public McpConnection_t
{
    int _Socket;
    int _WakeFd;

public:
    PosixConnection_t(int socket, int wakeFd):
        _Socket(socket),
        _WakeFd(wakeFd)
    {
    }

    ~PosixConnection_t() override
    {
        close(_Socket);
    }

    size_t Read(void* buffer, size_t size) override
    {
        pollfd fds[2] = { { _Socket, POLLIN, 0 }, { _WakeFd, POLLIN, 0 } };
        while (true)
        {
            const int res = poll(fds, _WakeFd >= 0 ? 2 : 1, -1);
            if (res < 0)
            {
                if (errno == EINTR)
                    continue;

                return 0;
            }

            if (_WakeFd >= 0 && fds[1].revents != 0)
                return 0;

            const ssize_t count = recv(_Socket, buffer, size, 0);
            if (count < 0 && errno == EINTR)
                continue;

            return count > 0 ? static_cast<size_t>(count) : 0;
        }
    }

    bool Write(void const* buffer, size_t size) override
    {
        auto cur = static_cast<char const*>(buffer);
        while (size != 0)
        {
            const ssize_t count = send(_Socket, cur, size, MSG_NOSIGNAL);
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }
            cur += count;
            size -= static_cast<size_t>(count);
        }
        return true;
    }
};

class PosixListener_t : public McpListener_t
{
    int _Socket;
    int _WakePipe[2];
    std::string _Path;

public:
    PosixListener_t(int socket, int wakePipe[2], std::string path):
        _Socket(socket),
        _WakePipe{ wakePipe[0], wakePipe[1] },
        _Path(std::move(path))
    {
    }

    ~PosixListener_t() override
    {
        close(_Socket);
        close(_WakePipe[0]);
        close(_WakePipe[1]);
        unlink(_Path.c_str());
    }

    std::unique_ptr<McpConnection_t> Accept() override
    {
        pollfd fds[2] = { { _Socket, POLLIN, 0 }, { _WakePipe[0], POLLIN, 0 } };
        while (true)
        {
            const int res = poll(fds, 2, -1);
            if (res < 0)
            {
                if (errno == EINTR)
                    continue;

                return nullptr;
            }

            if (fds[1].revents != 0)
                return nullptr;

            const int client = accept(_Socket, nullptr, nullptr);
            if (client < 0)
            {
                if (errno == EINTR || errno == ECONNABORTED)
                    continue;

                return nullptr;
            }

            fcntl(client, F_SETFD, FD_CLOEXEC);
            return std::make_unique<PosixConnection_t>(client, _WakePipe[0]);
        }
    }

    void Wake() override
    {
        // The pipe stays readable: this also unblocks whatever comes next.
        const char c = 0;
        while (write(_WakePipe[1], &c, 1) < 0 && errno == EINTR) {}
    }
};

}

std::unique_ptr<McpListener_t> McpListen(McpEndpoint_t const& endpoint)
{
    sockaddr_un address;
    std::string path;
    if (!GetSocketAddress(endpoint, address, path) || !CreatePrivateDirectory(GetSocketDirectory()))
        return nullptr;

    // Another overlay of this process already uses the name: taking its socket would leave it unreachable.
    if (IsSocketInUse(address))
        return nullptr;

    // The socket left by a crashed process which had the same pid and name.
    unlink(path.c_str());

    int wakePipe[2];
    if (pipe(wakePipe) != 0)
        return nullptr;

    fcntl(wakePipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(wakePipe[1], F_SETFD, FD_CLOEXEC);

    const int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server >= 0)
    {
        fcntl(server, F_SETFD, FD_CLOEXEC);
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
        {
            chmod(path.c_str(), 0600);
            if (listen(server, 1) == 0)
                return std::make_unique<PosixListener_t>(server, wakePipe, path);

            unlink(path.c_str());
        }
        close(server);
    }

    close(wakePipe[0]);
    close(wakePipe[1]);
    return nullptr;
}

std::unique_ptr<McpConnection_t> McpConnect(McpEndpoint_t const& endpoint)
{
    sockaddr_un address;
    std::string path;
    if (!GetSocketAddress(endpoint, address, path))
        return nullptr;

    const int client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0)
        return nullptr;

    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        close(client);
        return nullptr;
    }

    return std::make_unique<PosixConnection_t>(client, -1);
}

std::vector<McpEndpoint_t> McpFindServers()
{
    std::vector<McpEndpoint_t> servers;

    DIR* directory = opendir(GetSocketDirectory().c_str());
    if (directory == nullptr)
        return servers;

    while (dirent* entry = readdir(directory))
    {
        McpEndpoint_t endpoint;
        // A killed process leaves its socket behind.
        if (ParseSocketFileName(entry->d_name, endpoint) && kill(static_cast<pid_t>(endpoint.Pid), 0) == 0)
            servers.emplace_back(std::move(endpoint));
    }

    closedir(directory);
    return servers;
}

}
