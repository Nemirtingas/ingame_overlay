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

#include <InGameOverlay/RendererHook.h>

#include "McpTransport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace InGameOverlay {

class McpJson_t;
struct McpImGuiSnapshot_t;

// A request of the server thread, run by the render thread in OnFrame.
struct McpRenderTask_t
{
    std::function<std::string()> Function;
    std::string Result;
    bool Done = false;
};

struct McpPendingScreenshot_t
{
    bool Done = false;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Pitch = 0;
    ScreenshotDataFormat_t Format = ScreenshotDataFormat_t::Unknown;
    std::vector<uint8_t> Pixels;
};

struct McpToolResult_t
{
    std::string Content;
    bool IsError = false;
};

// The MCP server of a renderer hook, fed from the render thread by RendererHookInternal_t. It keeps the hook state while stopped, for
// the next Start. Not thread safe: the application must not start or stop it while the render thread runs a frame.
class McpServerInternal_t
{
    struct Tool_t
    {
        std::string Name, Description, Schema;
        McpToolCallback_t Call;
        void* UserParameter;
        std::recursive_mutex Mutex;
        bool Removed = false;
        bool Running = false;
    };
    std::mutex _ToolsMutex;
    std::vector<std::shared_ptr<Tool_t>> _Tools;
    std::mutex _OutputMutex;
    std::function<void(std::string const&)> _Output;
    std::atomic<bool> _Initialized{false};
    std::atomic<uint64_t> _Generation{0};
    std::thread _Notifier;
    RendererHook_t* _Hook = nullptr;
    McpServerCallbacks_t _Callbacks;
    std::unique_ptr<McpListener_t> _Listener;
    std::thread _Thread;
    std::atomic<bool> _Stopping{ false };
    std::atomic<uint64_t> _FrameCount{ 0 };
    std::atomic<int> _HookState{ -1 };
    std::string _Name;
    // Set when the application carries the messages instead of _Listener.
    McpServerTransport_t _Transport;

    std::mutex _Mutex;
    std::condition_variable _Condition;
    std::vector<std::shared_ptr<McpRenderTask_t>> _Tasks;
    std::shared_ptr<McpPendingScreenshot_t> _Screenshot;
    // The messages pushed by the transport, under _Mutex.
    std::deque<std::string> _Inbox;
    std::condition_variable _InboxCondition;

    // Without transport.Send, it listens on the endpoint of the process named after name.
    bool _Start(RendererHook_t* rendererHook, McpServerCallbacks_t const& callbacks, std::string const& name, McpServerTransport_t const& transport);
    void _RunServer();
    void _RunTransport();
    void _ServeConnection(McpConnection_t& connection);
    std::string _HandleLine(std::string const& line);
    std::string _HandleRequest(McpJson_t const& request);
    McpToolResult_t _CallTool(std::string const& name, McpJson_t const& arguments);

    bool _RunOnRenderThread(std::function<std::string()> function, std::string& result);
    McpToolResult_t _RunTool(std::function<std::string()> function);

    McpToolResult_t _ToolStatus();
    McpToolResult_t _ToolWaitFrames(McpJson_t const& arguments);
    McpToolResult_t _ToolScreenshot(McpJson_t const& arguments);
    McpToolResult_t _ToolMouse(McpJson_t const& arguments);
    McpToolResult_t _ToolKey(McpJson_t const& arguments);
    McpToolResult_t _ToolInputPolicy(McpJson_t const& arguments);

    bool _WaitForFrames(uint64_t count, std::chrono::milliseconds timeout);
    bool _CaptureSnapshot(McpImGuiSnapshot_t& snapshot, std::string& error);
    bool _CaptureWindows(McpImGuiSnapshot_t& snapshot);
    McpToolResult_t _ToolImGuiTree(McpJson_t const& arguments);
    McpToolResult_t _ToolImGuiFind(McpJson_t const& arguments);
    McpToolResult_t _ToolImGuiItem(McpJson_t const& arguments);
    McpToolResult_t _ToolImGuiWindow(McpJson_t const& arguments);
    McpToolResult_t _ToolImGuiDebugLog(McpJson_t const& arguments);

public:
    McpServerInternal_t() = default;
    bool AddTool(McpTool_t const& tool);
    bool RemoveTool(const char* name);
    ~McpServerInternal_t();

    McpServerInternal_t(McpServerInternal_t const&) = delete;
    McpServerInternal_t& operator=(McpServerInternal_t const&) = delete;

    // Starts listening. The renderer hook must outlive the server. false if it already runs, if the name is invalid, or if the endpoint
    // can't be created.
    bool Start(RendererHook_t* rendererHook, McpServerCallbacks_t callbacks = {}, std::string const& name = std::string());

    // Without the endpoint: the messages come from PushMessage and the answers go to transport.Send.
    bool StartWithTransport(RendererHook_t* rendererHook, McpServerTransport_t transport, McpServerCallbacks_t callbacks = {});

    // Queues a message of the client for the server thread. false without a transport, or if the message or the queue is too large.
    bool PushMessage(const char* message, size_t size);

    // Stops listening and waits for the server thread.
    void Stop();

    std::string const& GetName() const;

    // Runs the requests waiting for the render thread. Called inside the ImGui frame, before the overlay. Does nothing while stopped.
    void OnFrame();

    void OnHookState(OverlayHookState state);

    // true if the screenshot was requested by the MCP client, then the application must not get it.
    bool OnScreenshot(ScreenshotCallbackParameter_t const* screenshot);
};

}
