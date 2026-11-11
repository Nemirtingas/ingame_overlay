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

#include <Windows.h>
#include <sddl.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace InGameOverlay {

namespace {

constexpr DWORD PipeBufferSize = 64 * 1024;

constexpr wchar_t PipePrefix[] = L"ingame_overlay_mcp_";

// \\.\pipe\ingame_overlay_mcp_<pid> or \\.\pipe\ingame_overlay_mcp_<pid>_<name>. The name is ASCII.
std::wstring GetPipeName(McpEndpoint_t const& endpoint)
{
    std::wstring name = L"\\\\.\\pipe\\" + std::wstring(PipePrefix) + std::to_wstring(endpoint.Pid);
    if (!endpoint.Name.empty())
        name += L"_" + std::wstring(endpoint.Name.begin(), endpoint.Name.end());
    return name;
}

bool ParsePipeName(wchar_t const* pipeName, McpEndpoint_t& endpoint)
{
    const size_t prefixLength = wcslen(PipePrefix);
    if (wcsncmp(pipeName, PipePrefix, prefixLength) != 0)
        return false;

    wchar_t const* pid = pipeName + prefixLength;
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(pid, &end, 10);
    if (end == pid || value == 0)
        return false;

    endpoint.Pid = static_cast<uint32_t>(value);
    endpoint.Name.clear();
    if (*end == L'\0')
        return true;

    if (*end != L'_')
        return false;

    for (wchar_t const* c = end + 1; *c != L'\0'; ++c)
    {
        if (*c > 0x7F)
            return false;
        endpoint.Name += static_cast<char>(*c);
    }
    return !endpoint.Name.empty() && McpIsValidEndpointName(endpoint.Name);
}

// The default pipe DACL lets everyone read: only keep the current user.
PSECURITY_DESCRIPTOR CreateOwnerOnlyDescriptor()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return nullptr;

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string buffer(size, '\0');
    if (size != 0 && GetTokenInformation(token, TokenUser, &buffer[0], size, &size))
    {
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(&buffer[0])->User.Sid, &sid))
        {
            const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
            LocalFree(sid);
        }
    }

    CloseHandle(token);
    return descriptor;
}

class Event_t
{
public:
    HANDLE Handle;

    Event_t():
        Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    ~Event_t()
    {
        if (Handle != nullptr)
            CloseHandle(Handle);
    }
};

// Waits for an overlapped operation, cancels it when the stop event is raised. stopEvent can be nullptr.
bool WaitOverlapped(HANDLE pipe, OVERLAPPED& overlapped, HANDLE stopEvent, DWORD& transferred)
{
    HANDLE events[2] = { overlapped.hEvent, stopEvent };
    if (WaitForMultipleObjects(stopEvent != nullptr ? 2 : 1, events, FALSE, INFINITE) != WAIT_OBJECT_0)
    {
        CancelIoEx(pipe, &overlapped);
        GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
        return false;
    }

    return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
}

class WindowsConnection_t : public McpConnection_t
{
    HANDLE _Pipe;
    HANDLE _StopEvent;
    bool _IsServer;

public:
    WindowsConnection_t(HANDLE pipe, HANDLE stopEvent, bool isServer):
        _Pipe(pipe),
        _StopEvent(stopEvent),
        _IsServer(isServer)
    {
    }

    ~WindowsConnection_t() override
    {
        if (_IsServer)
            DisconnectNamedPipe(_Pipe);
        else
            CloseHandle(_Pipe);
    }

    size_t Read(void* buffer, size_t size) override
    {
        Event_t event;
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.Handle;

        DWORD transferred = 0;
        if (!ReadFile(_Pipe, buffer, static_cast<DWORD>(size), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
            return 0;

        return WaitOverlapped(_Pipe, overlapped, _StopEvent, transferred) ? transferred : 0;
    }

    bool Write(void const* buffer, size_t size) override
    {
        auto cur = static_cast<char const*>(buffer);
        while (size != 0)
        {
            Event_t event;
            OVERLAPPED overlapped{};
            overlapped.hEvent = event.Handle;

            const DWORD chunk = size > PipeBufferSize ? PipeBufferSize : static_cast<DWORD>(size);
            DWORD transferred = 0;
            if (!WriteFile(_Pipe, cur, chunk, nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
                return false;

            if (!WaitOverlapped(_Pipe, overlapped, _StopEvent, transferred))
                return false;

            cur += transferred;
            size -= transferred;
        }
        return true;
    }
};

class WindowsListener_t : public McpListener_t
{
    HANDLE _Pipe;
    Event_t _StopEvent;

public:
    explicit WindowsListener_t(HANDLE pipe):
        _Pipe(pipe)
    {
    }

    ~WindowsListener_t() override
    {
        CloseHandle(_Pipe);
    }

    std::unique_ptr<McpConnection_t> Accept() override
    {
        Event_t event;
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.Handle;

        if (!ConnectNamedPipe(_Pipe, &overlapped))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING)
            {
                DWORD transferred = 0;
                if (!WaitOverlapped(_Pipe, overlapped, _StopEvent.Handle, transferred))
                    return nullptr;
            }
            else if (error != ERROR_PIPE_CONNECTED)
            {
                return nullptr;
            }
        }

        return std::make_unique<WindowsConnection_t>(_Pipe, _StopEvent.Handle, true);
    }

    void Wake() override
    {
        SetEvent(_StopEvent.Handle);
    }
};

}

std::unique_ptr<McpListener_t> McpListen(McpEndpoint_t const& endpoint)
{
    if (!McpIsValidEndpointName(endpoint.Name))
        return nullptr;

    PSECURITY_DESCRIPTOR descriptor = CreateOwnerOnlyDescriptor();
    if (descriptor == nullptr)
        return nullptr;

    SECURITY_ATTRIBUTES attributes{ sizeof(attributes), descriptor, FALSE };

    // Only one instance, and it has to be ours: this fails if somebody else already squats the name.
    HANDLE pipe = CreateNamedPipeW(
        GetPipeName(endpoint).c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        PipeBufferSize,
        PipeBufferSize,
        0,
        &attributes);

    LocalFree(descriptor);

    if (pipe == INVALID_HANDLE_VALUE)
        return nullptr;

    return std::make_unique<WindowsListener_t>(pipe);
}

std::unique_ptr<McpConnection_t> McpConnect(McpEndpoint_t const& endpoint)
{
    if (!McpIsValidEndpointName(endpoint.Name))
        return nullptr;

    // SECURITY_SQOS_PRESENT: a process squatting the name must not be able to impersonate us.
    HANDLE pipe = CreateFileW(
        GetPipeName(endpoint).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
        return nullptr;

    return std::make_unique<WindowsConnection_t>(pipe, nullptr, false);
}

std::vector<McpEndpoint_t> McpFindServers()
{
    std::vector<McpEndpoint_t> servers;

    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(L"\\\\.\\pipe\\ingame_overlay_mcp_*", &data);
    if (find == INVALID_HANDLE_VALUE)
        return servers;

    do
    {
        McpEndpoint_t endpoint;
        if (ParsePipeName(data.cFileName, endpoint))
            servers.emplace_back(std::move(endpoint));
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return servers;
}

}
