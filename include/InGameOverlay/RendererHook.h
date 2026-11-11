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

#include <functional>
#include <cstddef>
#include <cstdint>
#include <string>

#include "RendererResource.h"

namespace InGameOverlay {

enum class ToggleKey
{
    SHIFT, CTRL, ALT,
    TAB,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

enum class OverlayHookState : uint8_t
{
    Ready,
    Reset,
    Removing,
};

/// <summary>
/// Only one RendererHookType_t will always be returned by RendererHook_t::GetRendererHookType
/// but you can use them as flags to limit the detection in InGameOverlay::DetectRenderer.
/// NOTE: Even if a DirectX hook has been found and DXVK is used, the renderer hook will only report the DirectX hook type.
/// There is no combination like : DirectX11 | Vulkan returned by RendererHook_t::GetRendererHookType.
/// </summary>
enum class RendererHookType_t : uint8_t
{
    DirectX9   = 1 << 0,
    DirectX10  = 1 << 1,
    DirectX11  = 1 << 2,
    DirectX12  = 1 << 3,
    OpenGL     = 1 << 4,
    Vulkan     = 1 << 5,
    Metal      = 1 << 6,
    AnyDirectX = DirectX9 | DirectX10 | DirectX11 | DirectX12,
    AnyWindows = DirectX9 | DirectX10 | DirectX11 | DirectX12 | OpenGL | Vulkan,
    AnyLinux   = OpenGL | Vulkan,
    AnyMacOS   = OpenGL | Metal,
    Any        = DirectX9 | DirectX10 | DirectX11 | DirectX12 | OpenGL | Vulkan | Metal,
};

enum class ScreenshotType_t : uint8_t
{
    None = 0,
    BeforeOverlay = 1,
    AfterOverlay = 2,
};

enum class ScreenshotDataFormat_t : uint16_t
{
    Unknown = 0,
    // 8-bit formats
    R8G8B8,
    X8R8G8B8,
    A8R8G8B8,
    B8G8R8A8,
    B8G8R8X8,
    R8G8B8A8,

    // 10-bit formats
    A2R10G10B10,
    A2B10G10R10,
    R10G10B10A2,

    // 16-bit formats
    R5G6B5,
    X1R5G5B5,
    A1R5G5B5,
    B5G6R5,
    B5G5R5A1,

    // HDR / float formats
    R16G16B16A16_FLOAT,
    R16G16B16A16_UNORM,
    R32G32B32A32_FLOAT,
};

/// <summary>
///   The screenshot pixels data and format. To copy this buffer, you can reserve Width * Height * PixelSize bytes, but you will need to
///   copy row by row because of the pitch.
/// </summary>
struct ScreenshotCallbackParameter_t
{
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelSize;
    uint32_t Pitch;
    void* Data;
    ScreenshotDataFormat_t Format;
};

typedef void (*ScreenshotCallback_t)(ScreenshotCallbackParameter_t const* screenshot, void* userParameter);

typedef bool (*McpIsOverlayVisibleCallback_t)(void* userParameter);
typedef void (*McpSetOverlayVisibleCallback_t)(bool visible, void* userParameter);

/// <summary>
///   Optional callbacks letting the MCP client control what only the application knows about. They run on the render thread,
///   before OverlayProc, and receive UserParameter.
/// </summary>
struct McpServerCallbacks_t
{
    McpIsOverlayVisibleCallback_t IsOverlayVisible = nullptr;
    McpSetOverlayVisibleCallback_t SetOverlayVisible = nullptr;
    void* UserParameter = nullptr;
};

typedef void (*McpSendMessageCallback_t)(const char* message, size_t size, void* userParameter);

/// <summary>
///   The application's own transport for a MCP server started with StartMcpServerWithTransport.
/// </summary>
struct McpServerTransport_t
{
    /// <summary>
    ///   Required. Receives each message for the client: one JSON-RPC message in UTF-8, without newline, not null terminated. Called on
    ///   the server thread, one message at a time; it may call PushMcpMessage.
    /// </summary>
    McpSendMessageCallback_t Send = nullptr;
    void* UserParameter = nullptr;
};

/// <summary>
///   The renderer hook.
///     ResourceAutoLoad_t: Default value is ResourceAutoLoad_t::Batch
///     BatchSize: Default value is 10
/// </summary>
class RendererHook_t
{
protected:
    // Use Release: the MCP server must stop before the hook is destroyed.
    virtual ~RendererHook_t() {}

public:
    /// <summary>
    ///   Stops the MCP server and destroys the renderer hook. Don't call this while holding the Windows loader lock during a FreeLibrary
    ///   if a MCP server runs: it waits for its thread.
    /// </summary>
    virtual void Release() = 0;

    // TODO: Deprecated direct use of thoses and use either a setter or plain C function pointers with void* user parameter.
    std::function<void()> OverlayProc;
    std::function<void(OverlayHookState)> OverlayHookReady;

    virtual void SetScreenshotCallback(ScreenshotCallback_t callback, void* userParam) = 0;

    /// <summary>
    ///   Starts the current renderer hook procedure, allowing a user to render things on the application window.
    /// </summary>
    /// <param name="key_combination_callback">
    ///   Callback called when your toggle_keys are all pressed.
    /// </param>
    /// <param name="toggle_keys">
    ///   The key combination that must be pressed to show/hide your overlay.
    /// </param>
    /// <param name="imgui_font_atlas">
    ///   *Can be nullptr*. Fill this parameter with your own ImGuiAtlas pointer if you don't want ImGui to generate one for you.
    /// </param>
    /// <returns></returns>
    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr) = 0;

    /// <summary>
    ///   Change the hooked application input policy.
    /// </summary>
    /// <param name="hide">
    ///   Set to true to hide mouse and keyboards inputs from the hooked application.
    ///   Set to false to allow the hooked application to receive inputs.
    /// </param>
    /// <returns></returns>
    virtual void HideAppInputs(bool hide) = 0;
    
    /// <summary>
    ///   Change the overlay input policy.
    /// </summary>
    /// <param name="hide">
    ///   Set to true to hide mouse and keyboards inputs from the overlay.
    ///   Set to false to allow the overlay to receive inputs.
    /// </param>
    /// <returns></returns>
    virtual void HideOverlayInputs(bool hide) = 0;

    /// <summary>
    ///   Returns the hook state. If its started, then the functions are hooked (redirected to InGameOverlay) and will intercepts the application frame rendering.
    /// </summary>
    /// <returns></returns>
    virtual bool IsStarted() = 0;

    /// <summary>
    ///   Get the current renderer library name.
    /// </summary>
    /// <returns></returns>
    virtual const char* GetLibraryName() const = 0;

    /// <summary>
    ///   Get the current renderer hook type.
    /// </summary>
    /// <returns></returns>
    virtual RendererHookType_t GetRendererHookType() const = 0;

    /// <summary>
    ///   Gets the auto load batch size.
    /// </summary>
    /// <returns></returns>
    virtual uint32_t GetAutoLoadBatchSize() = 0;

    /// <summary>
    ///   Sets how many resources the renderer hook can load before rendering the frame, to not block the rendering with a hundred of resource to load.
    /// </summary>
    /// <param name="batchSize"></param>
    virtual void SetAutoLoadBatchSize(uint32_t batchSize) = 0;

    /// <summary>
    ///   Creates an image resource that can be setup and used later.
    /// </summary>
    /// <returns></returns>
    virtual RendererResource_t* CreateResource() = 0;

    /// <summary>
    ///   Creates an image resource and attach the data to it.
    /// </summary>
    /// <param name="image_data">
    ///   The RGBA buffer.
    /// </param>
    /// <param name="width">
    ///   Your RGBA image width.
    /// </param>
    /// <param name="height">
    ///   Your RGBA image height.
    /// </param>
    /// <returns></returns>
    virtual RendererResource_t* CreateAndAttachResource(const void* image_data, uint32_t width, uint32_t height) = 0;

    virtual void TakeScreenshot(ScreenshotType_t type) = 0;

    /// <summary>
    ///   Starts a Model Context Protocol server to test and control this overlay from an agent, one client at a time. The renderer hook
    ///   feeds it with its frames, its state and the screenshots the client asks for (your screenshot callback doesn't get them).
    ///   It runs until StopMcpServer or Release. Do not ship it: whoever reaches the endpoint controls the overlay.
    ///   Endpoints, only accepting the user running the process:
    ///     Linux:   Unix socket $XDG_RUNTIME_DIR/ingame_overlay/mcp-(pid).sock, or mcp-(pid)-(name).sock
    ///     Windows: named pipe \\.\pipe\ingame_overlay_mcp_(pid), or ingame_overlay_mcp_(pid)_(name)
    ///     macOS:   bootstrap service com.nemirtingas.ingame_overlay.mcp.(pid), or com.nemirtingas.ingame_overlay.mcp.(pid).(name)
    ///   Messages are newline delimited JSON-RPC, use ingame_overlay_mcp_bridge to expose them on stdio to a MCP client.
    /// </summary>
    /// <param name="name">
    ///   Tells apart the overlays of a same process, each one needs its own. Up to 32 letters, digits, '-' or '_'.
    ///   Empty for the default endpoint of the process.
    /// </param>
    /// <param name="callbacks">
    ///   Lets the MCP client show or hide the overlay.
    /// </param>
    /// <returns>
    ///   false if the library was built without INGAMEOVERLAY_BUILD_MCP, if a server already runs, if the name is invalid, or if the
    ///   endpoint can't be created (already used by another overlay of the process).
    /// </returns>
    virtual bool StartMcpServer(std::string const& name = std::string(), McpServerCallbacks_t callbacks = {}) = 0;

    /// <summary>
    ///   Starts the same MCP server without its endpoint: the application carries the messages with its own protocol, giving the
    ///   client's messages to PushMcpMessage and sending the server's ones from transport.Send.
    /// </summary>
    /// <returns>
    ///   false if the library was built without INGAMEOVERLAY_BUILD_MCP, if a server already runs, or if transport.Send is null.
    /// </returns>
    virtual bool StartMcpServerWithTransport(McpServerTransport_t transport, McpServerCallbacks_t callbacks = {}) = 0;

    /// <summary>
    ///   Gives a message of the client to the MCP server started with StartMcpServerWithTransport: one JSON-RPC message in UTF-8, up to
    ///   1 MiB. It is queued and handled on the server thread, in order; the answer, if any, goes to transport.Send.
    ///   Don't call it from the render thread and wait for the answer there: tools wait for the next frames.
    /// </summary>
    /// <returns>
    ///   false if no server runs with a transport, if the message is empty or too large, or if too many messages are waiting.
    /// </returns>
    virtual bool PushMcpMessage(const char* message, size_t size) = 0;

    /// <summary>
    ///   Stops the MCP server, if any, and waits for its thread. The messages it hasn't handled yet are dropped.
    /// </summary>
    virtual void StopMcpServer() = 0;
};

}

inline InGameOverlay::RendererHookType_t operator&(InGameOverlay::RendererHookType_t l, InGameOverlay::RendererHookType_t r)
{
    return static_cast<InGameOverlay::RendererHookType_t>(static_cast<uint8_t>(l) & static_cast<uint8_t>(r));
}

inline InGameOverlay::RendererHookType_t operator|(InGameOverlay::RendererHookType_t l, InGameOverlay::RendererHookType_t r)
{
    return static_cast<InGameOverlay::RendererHookType_t>(static_cast<uint8_t>(l) | static_cast<uint8_t>(r));
}