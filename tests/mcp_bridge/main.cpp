// Exposes the MCP server of a running overlay on stdio, which is what MCP clients know how to launch:
//   ingame_overlay_mcp_bridge [--pid <pid>] [--wait <seconds>]
//   ingame_overlay_mcp_bridge --list
// Through ssh, this also gives access to an overlay running on another machine.

#include "McpTransport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace InGameOverlay;

static int ReadStdin(char* buffer, unsigned size)
{
#if defined(_WIN32)
    return _read(0, buffer, size);
#else
    return static_cast<int>(read(0, buffer, size));
#endif
}

static bool WriteStdout(char const* buffer, size_t size)
{
    while (size != 0)
    {
#if defined(_WIN32)
        const int count = _write(1, buffer, static_cast<unsigned>(size > 65536 ? 65536 : size));
#else
        const int count = static_cast<int>(write(1, buffer, size > 65536 ? 65536 : size));
#endif
        if (count <= 0)
            return false;

        buffer += count;
        size -= static_cast<size_t>(count);
    }
    return true;
}

// name is nullptr when any name matches.
static std::unique_ptr<McpConnection_t> FindAndConnect(uint32_t pid, char const* name, std::string& error)
{
    // Both known: no need to list, which macOS can't do anyway.
    if (pid != 0 && name != nullptr)
    {
        McpEndpoint_t endpoint{ pid, name };
        auto connection = McpConnect(endpoint);
        if (connection == nullptr)
            error = "No overlay is listening on " + McpEndpointToString(endpoint);

        return connection;
    }

    // A crashed process leaves its socket behind, that's why a server counts only if we can connect to it.
    std::vector<McpEndpoint_t> servers;
    std::unique_ptr<McpConnection_t> found;
    for (auto const& server : McpFindServers())
    {
        if ((pid != 0 && server.Pid != pid) || (name != nullptr && server.Name != name))
            continue;

        auto connection = McpConnect(server);
        if (connection != nullptr)
        {
            found = std::move(connection);
            servers.emplace_back(server);
        }
    }

    if (servers.size() > 1)
    {
        error = "Several overlays are running, select one with --pid and/or --name:";
        for (auto const& server : servers)
            error += " " + McpEndpointToString(server);

        return nullptr;
    }

    if (found == nullptr)
    {
#if defined(__APPLE__)
        error = "macOS can't list the overlays, give --pid (and --name if the overlay has one).";
#else
        error = "No overlay is listening, is INGAMEOVERLAY_MCP set for the application?";
#endif
    }

    return found;
}

int main(int argc, char* argv[])
{
    uint32_t pid = 0;
    char const* name = nullptr;
    int waitSeconds = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc)
        {
            pid = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        }
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
        {
            name = argv[++i];
            if (!McpIsValidEndpointName(name))
            {
                fprintf(stderr, "Invalid name: up to %u letters, digits, '-' or '_'.\n", static_cast<unsigned>(McpMaxEndpointNameLength));
                return 2;
            }
        }
        else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc)
        {
            waitSeconds = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--list") == 0)
        {
            // One line per overlay: the pid, then the name if it has one.
            for (auto const& server : McpFindServers())
                printf("%u%s%s\n", server.Pid, server.Name.empty() ? "" : " ", server.Name.c_str());

            return 0;
        }
        else
        {
            fprintf(stderr, "Usage: %s [--pid <pid>] [--name <name>] [--wait <seconds>] | --list\n"
                "  --name \"\" selects the overlay without a name.\n", argv[0]);
            return 2;
        }
    }

#if defined(_WIN32)
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
#endif

    std::string error;
    auto connection = FindAndConnect(pid, name, error);
    const auto expiration = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
    while (connection == nullptr && std::chrono::steady_clock::now() < expiration)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        connection = FindAndConnect(pid, name, error);
    }

    if (connection == nullptr)
    {
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // Whichever side ends first ends the bridge, the blocked thread goes away with the process.
    std::thread([&connection]()
    {
        char buffer[4096];
        int count;
        while ((count = ReadStdin(buffer, sizeof(buffer))) > 0)
        {
            if (!connection->Write(buffer, static_cast<size_t>(count)))
                break;
        }
        fflush(nullptr);
        _Exit(0);
    }).detach();

    char buffer[4096];
    size_t count;
    while ((count = connection->Read(buffer, sizeof(buffer))) != 0)
    {
        if (!WriteStdout(buffer, count))
            break;
    }

    _Exit(0);
}
