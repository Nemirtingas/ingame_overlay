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

#include "McpServerInternal.h"

#ifdef INGAMEOVERLAY_MCP_IMGUI_INSPECTOR
#include "McpImGuiInspector.h"
#endif
#include "McpJson.h"
#include "McpPng.h"
#include "McpTransport.h"

#include <imgui.h>
#include <imgui_internal.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace InGameOverlay {

namespace {

constexpr size_t MaxMessageSize = 1024 * 1024;
// Messages pushed by the application's transport and not handled yet.
constexpr size_t MaxPendingMessages = 64;
constexpr auto RenderThreadTimeout = std::chrono::seconds(5);
constexpr auto ScreenshotTimeout = std::chrono::seconds(10);
constexpr int DefaultMaxScreenshotDimension = 1024;

constexpr char const* ProtocolVersions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };

McpToolResult_t TextResult(std::string const& text, bool isError = false)
{
    return McpToolResult_t{ "[{\"type\":\"text\",\"text\":" + McpJsonQuote(text) + "}]", isError };
}

char const* RendererTypeName(RendererHookType_t type)
{
    switch (type)
    {
        case RendererHookType_t::DirectX9:  return "DirectX9";
        case RendererHookType_t::DirectX10: return "DirectX10";
        case RendererHookType_t::DirectX11: return "DirectX11";
        case RendererHookType_t::DirectX12: return "DirectX12";
        case RendererHookType_t::OpenGL:    return "OpenGL";
        case RendererHookType_t::Vulkan:    return "Vulkan";
        case RendererHookType_t::Metal:     return "Metal";
        default:                            return "Unknown";
    }
}

uint32_t CurrentProcessId()
{
#if defined(_WIN32)
    return static_cast<uint32_t>(_getpid());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

uint8_t FloatToByte(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

float HalfToFloat(uint16_t half)
{
    const int exponent = (half >> 10) & 0x1F;
    const float mantissa = static_cast<float>(half & 0x3FF) / 1024.0f;
    const float magnitude = exponent == 0
        ? std::ldexp(mantissa, -14)
        : std::ldexp(1.0f + mantissa, exponent - 15);
    return (half & 0x8000) != 0 ? -magnitude : magnitude;
}

// Writes r, g, b. The alpha of a backbuffer doesn't mean anything, so it is dropped.
bool ConvertPixel(ScreenshotDataFormat_t format, uint8_t const* src, uint8_t* dst)
{
    switch (format)
    {
        case ScreenshotDataFormat_t::R8G8B8:
        case ScreenshotDataFormat_t::R8G8B8A8:
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            return true;

        // These names are the DirectX ones, which are memory ordered like this on a little endian machine.
        case ScreenshotDataFormat_t::X8R8G8B8:
        case ScreenshotDataFormat_t::A8R8G8B8:
        case ScreenshotDataFormat_t::B8G8R8A8:
        case ScreenshotDataFormat_t::B8G8R8X8:
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            return true;

        case ScreenshotDataFormat_t::R16G16B16A16_UNORM:
        {
            uint16_t channels[3];
            memcpy(channels, src, sizeof(channels));
            for (int i = 0; i < 3; ++i)
                dst[i] = static_cast<uint8_t>(channels[i] >> 8);
            return true;
        }

        case ScreenshotDataFormat_t::R32G32B32A32_FLOAT:
        {
            float channels[3];
            memcpy(channels, src, sizeof(channels));
            for (int i = 0; i < 3; ++i)
                dst[i] = FloatToByte(channels[i]);
            return true;
        }

        case ScreenshotDataFormat_t::R16G16B16A16_FLOAT:
        {
            uint16_t channels[3];
            memcpy(channels, src, sizeof(channels));
            for (int i = 0; i < 3; ++i)
                dst[i] = FloatToByte(HalfToFloat(channels[i]));
            return true;
        }

        default:
            return false;
    }
}

uint32_t PixelSizeOf(ScreenshotDataFormat_t format)
{
    switch (format)
    {
        case ScreenshotDataFormat_t::R8G8B8:             return 3;
        case ScreenshotDataFormat_t::R8G8B8A8:
        case ScreenshotDataFormat_t::X8R8G8B8:
        case ScreenshotDataFormat_t::A8R8G8B8:
        case ScreenshotDataFormat_t::B8G8R8A8:
        case ScreenshotDataFormat_t::B8G8R8X8:           return 4;
        case ScreenshotDataFormat_t::R16G16B16A16_FLOAT:
        case ScreenshotDataFormat_t::R16G16B16A16_UNORM: return 8;
        case ScreenshotDataFormat_t::R32G32B32A32_FLOAT: return 16;
        default:                                         return 0;
    }
}

// Box filter, otherwise the text of the overlay becomes unreadable when the screenshot is reduced.
bool ConvertScreenshot(McpPendingScreenshot_t const& screenshot, uint32_t maxDimension, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgb)
{
    const uint32_t pixelSize = PixelSizeOf(screenshot.Format);
    if (pixelSize == 0 || screenshot.Width == 0 || screenshot.Height == 0 || screenshot.Pitch < screenshot.Width * pixelSize)
        return false;

    const uint32_t largest = std::max(screenshot.Width, screenshot.Height);
    const double scale = largest > maxDimension ? static_cast<double>(maxDimension) / largest : 1.0;
    width = std::max<uint32_t>(1, static_cast<uint32_t>(screenshot.Width * scale));
    height = std::max<uint32_t>(1, static_cast<uint32_t>(screenshot.Height * scale));

    rgb.resize(size_t(width) * height * 3);
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint32_t y0 = static_cast<uint32_t>(uint64_t(y) * screenshot.Height / height);
        const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(uint64_t(y + 1) * screenshot.Height / height));
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t x0 = static_cast<uint32_t>(uint64_t(x) * screenshot.Width / width);
            const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(uint64_t(x + 1) * screenshot.Width / width));

            uint32_t sum[3] = {};
            for (uint32_t sy = y0; sy < y1; ++sy)
            {
                uint8_t const* row = screenshot.Pixels.data() + size_t(sy) * screenshot.Pitch;
                for (uint32_t sx = x0; sx < x1; ++sx)
                {
                    uint8_t pixel[3];
                    ConvertPixel(screenshot.Format, row + size_t(sx) * pixelSize, pixel);
                    for (int i = 0; i < 3; ++i)
                        sum[i] += pixel[i];
                }
            }

            const uint32_t count = (x1 - x0) * (y1 - y0);
            uint8_t* dst = rgb.data() + (size_t(y) * width + x) * 3;
            for (int i = 0; i < 3; ++i)
                dst[i] = static_cast<uint8_t>(sum[i] / count);
        }
    }
    return true;
}

struct ToolDefinition_t
{
    char const* Name;
    char const* Description;
    char const* InputSchema;
};

constexpr char const* EmptySchema = R"({"type":"object","properties":{},"additionalProperties":false})";

constexpr ToolDefinition_t Tools[] = {
    { "overlay_status", "Returns the renderer, the hook state, the frame count, the display size and the ImGui backends.", EmptySchema },
    { "overlay_wait_frames", "Waits for the application to render more frames, to let a previous action take effect.",
        R"({"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":10000,"default":1},"timeout_ms":{"type":"integer","minimum":1,"maximum":60000,"default":5000}},"additionalProperties":false})" },
    { "overlay_windows", "Lists the ImGui windows of the overlay with their position, size and state.", EmptySchema },
    { "overlay_screenshot", "Captures the application window as a PNG image.",
        R"({"type":"object","properties":{"include_overlay":{"type":"boolean","default":true,"description":"Capture after the overlay has been rendered."},"max_dimension":{"type":"integer","minimum":16,"maximum":8192,"default":1024,"description":"The image is reduced to fit into this size."}},"additionalProperties":false})" },
    { "overlay_mouse", "Sends a mouse event to the overlay, the application doesn't receive it.",
        R"({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"action":{"type":"string","enum":["move","click","down","up"],"default":"move"},"button":{"type":"integer","minimum":0,"maximum":4,"default":0}},"required":["x","y"],"additionalProperties":false})" },
    { "overlay_key", "Sends a key event to the overlay, the application doesn't receive it.",
        R"({"type":"object","properties":{"key":{"type":"string","description":"An ImGui key name: A, Enter, Tab, Escape, LeftCtrl..."},"action":{"type":"string","enum":["press","down","up"],"default":"press"}},"required":["key"],"additionalProperties":false})" },
    { "overlay_text", "Types UTF-8 text in the focused ImGui input.",
        R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"],"additionalProperties":false})" },
    { "overlay_input_policy", "Sets who receives the real user inputs.",
        R"({"type":"object","properties":{"hide_from_app":{"type":"boolean"},"hide_from_overlay":{"type":"boolean"}},"additionalProperties":false})" },
    { "overlay_set_visible", "Shows or hides the overlay, if the application supports it.",
        R"({"type":"object","properties":{"visible":{"type":"boolean"}},"required":["visible"],"additionalProperties":false})" },
#ifdef INGAMEOVERLAY_MCP_IMGUI_INSPECTOR
    { "imgui_tree",
        "Captures one frame and returns the ImGui windows, back to front, with their child windows and their items as trees. "
        "Each item has a type (Button, Checkbox, Slider, Drag, InputText, InputScalar, Combo, TreeNode, CollapsingHeader, TabItem, Menu, MenuItem, "
        "Selectable, RadioButton, ColorButton, Text...), an id to give to imgui_item, a label, a value, the displayed text, a rect [x, y, width, height] "
        "in pixels and a state (checked, open, selected, writable, disabled, hovered, active, focused, clipped). Texts have no id.",
        R"({"type":"object","properties":{"window":{"type":"string","description":"Only this window and its child windows, by name."},"include_clipped":{"type":"boolean","default":false,"description":"Include the items scrolled out of their window."},"include_inactive":{"type":"boolean","default":false,"description":"Include the windows not submitted during the frame."},"include_decorations":{"type":"boolean","default":false,"description":"Include the title bars, resize grips and borders, scrollbars, close and collapse buttons."}},"additionalProperties":false})" },
    { "imgui_find", "Searches the items of the current frame by label or displayed text (case insensitive substring), type or window. Returns a flat list with their path. Window decorations (TitleBar, CloseButton...) are only listed when searched by type.",
        R"({"type":"object","properties":{"query":{"type":"string"},"type":{"type":"string","description":"Widget type, e.g. Button."},"window":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":1000,"default":50}},"additionalProperties":false})" },
    { "imgui_item",
        "Acts on an item designated by its id, or by its label (and window when several items share it). Returns the item before and after the action. "
        "click/double_click/right_click/hover move the mouse on the item (scrolled into view first) and bring its window to front. "
        "activate presses the item like the keyboard would, check/uncheck/open/close activate it only when needed. "
        "set_value writes a number or boolean in a writable widget (Slider, Drag, InputScalar, Checkbox), the application sees a normal edition. "
        "set_text replaces the content of a text input (InputText, or Slider/Drag/InputScalar through their text mode). "
        "To pick a Combo option, click the combo then click the option. info only returns the item.",
        R"({"type":"object","properties":{"id":{"type":"string","description":"\"0x...\" id from imgui_tree or imgui_find."},"label":{"type":"string"},"window":{"type":"string"},"action":{"type":"string","enum":["info","click","double_click","right_click","hover","activate","focus","scroll_to","check","uncheck","open","close","set_value","set_text"],"default":"click"},"value":{"type":["number","boolean"],"description":"For set_value."},"clamp":{"type":"boolean","default":true,"description":"For set_value, keep the value in the widget range."},"text":{"type":"string","description":"For set_text."},"submit":{"type":"boolean","default":true,"description":"For set_text, press Enter after typing (ignored by multiline inputs)."}},"additionalProperties":false})" },
    { "imgui_window", "Reads or changes an ImGui window. close presses its close button, the application decides what happens.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"The window name, with or without its ## suffix."},"action":{"type":"string","enum":["info","focus","collapse","expand","move","resize","scroll","close"],"default":"info"},"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"number"},"height":{"type":"number"}},"required":["name"],"additionalProperties":false})" },
    { "imgui_state", "Returns the ImGui context state to debug inputs and focus: mouse, keys down, hovered/focused/active window and item, open popups, edited text input, drag and drop, metrics.", EmptySchema },
    { "imgui_debug_log", "Enables or disables categories of the ImGui debug log (error, activeid, focus, popup, nav, clipper, selection, io, font, inputrouting, table) and returns its end.",
        R"({"type":"object","properties":{"enable":{"type":"array","items":{"type":"string"}},"disable":{"type":"array","items":{"type":"string"}},"clear":{"type":"boolean","default":false},"max_bytes":{"type":"integer","minimum":256,"maximum":1048576,"default":16384}},"additionalProperties":false})" },
#endif
};

constexpr char const* NoFrameError = "The application doesn't render any frame.";
constexpr int MaxCaptureAttempts = 10;
constexpr int MaxWriteAttempts = 30;
constexpr int MaxTextInputAttempts = 10;

}

bool McpServerInternal_t::AddTool(McpTool_t const& tool)
{
    if (!tool.Name || !tool.Description || !tool.InputSchema || !tool.Call)
        return false;
    std::string name(tool.Name);
    if (name.empty() || name.size() > 128 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-." ) != std::string::npos)
        return false;
    McpJson_t schema;
    if (!McpJson_t::Parse(tool.InputSchema, schema) || !schema.IsObject())
        return false;
    for (auto const& builtin : Tools)
        if (name == builtin.Name) return false;
    std::lock_guard<std::mutex> lock(_ToolsMutex);
    for (auto const& entry : _Tools)
        if (entry->Name == name) return false;
    auto entry = std::make_shared<Tool_t>();
    entry->Name = name;
    entry->Description = tool.Description;
    entry->Schema = tool.InputSchema;
    entry->Call = tool.Call;
    entry->UserParameter = tool.UserParameter;
    _Tools.push_back(entry);
    ++_Generation;
    return true;
}
bool McpServerInternal_t::RemoveTool(const char* name)
{
    if (!name) return false;
    std::shared_ptr<Tool_t> entry;
    {
        std::lock_guard<std::mutex> lock(_ToolsMutex);
        for (auto const& candidate : _Tools)
            if (candidate->Name == name) entry = candidate;
    }
    if (!entry) return false;
    // Never hold the registry while waiting for a callback (which may add/remove tools).
    std::lock_guard<std::recursive_mutex> callLock(entry->Mutex);
    if (entry->Running || entry->Removed) return false;
    std::lock_guard<std::mutex> lock(_ToolsMutex);
    entry->Removed = true;
    _Tools.erase(std::remove(_Tools.begin(), _Tools.end(), entry), _Tools.end());
    ++_Generation;
    return true;
}

bool McpServerInternal_t::_RunOnRenderThread(std::function<std::string()> function, std::string& result)
{
    auto task = std::make_shared<McpRenderTask_t>();
    task->Function = std::move(function);

    std::unique_lock<std::mutex> lock(_Mutex);
    _Tasks.emplace_back(task);
    _Condition.wait_for(lock, RenderThreadTimeout, [&]() { return task->Done || _Stopping.load(); });
    if (!task->Done)
    {
        // It must not run later: the client has been told that it failed.
        _Tasks.erase(std::remove(_Tasks.begin(), _Tasks.end(), task), _Tasks.end());
        return false;
    }

    result = std::move(task->Result);
    return true;
}

McpToolResult_t McpServerInternal_t::_RunTool(std::function<std::string()> function)
{
    std::string result;
    if (!_RunOnRenderThread(std::move(function), result))
        return TextResult("The application doesn't render any frame.", true);

    return TextResult(result);
}

McpToolResult_t McpServerInternal_t::_ToolStatus()
{
    std::string frameInfos;
    if (!_RunOnRenderThread([this]()
    {
        auto& io = ImGui::GetIO();
        std::string infos = "\"frame_running\":true,\"display_width\":" + McpJsonFloat(io.DisplaySize.x) +
            ",\"display_height\":" + McpJsonFloat(io.DisplaySize.y) +
            ",\"framerate\":" + McpJsonFloat(io.Framerate) +
            ",\"backend_renderer\":" + McpJsonQuote(io.BackendRendererName != nullptr ? io.BackendRendererName : "") +
            ",\"backend_platform\":" + McpJsonQuote(io.BackendPlatformName != nullptr ? io.BackendPlatformName : "") +
            ",\"want_capture_mouse\":" + (io.WantCaptureMouse ? "true" : "false") +
            ",\"want_capture_keyboard\":" + (io.WantCaptureKeyboard ? "true" : "false");

        if (_Callbacks.IsOverlayVisible != nullptr)
            infos += std::string(",\"overlay_visible\":") + (_Callbacks.IsOverlayVisible(_Callbacks.UserParameter) ? "true" : "false");

        return infos;
    }, frameInfos))
    {
        frameInfos = "\"frame_running\":false";
    }

    char const* hookState = "Unknown";
    switch (_HookState.load())
    {
        case static_cast<int>(OverlayHookState::Ready):    hookState = "Ready";    break;
        case static_cast<int>(OverlayHookState::Reset):    hookState = "Reset";    break;
        case static_cast<int>(OverlayHookState::Removing): hookState = "Removing"; break;
    }

    return TextResult(
        "{\"pid\":" + std::to_string(CurrentProcessId()) +
        ",\"name\":" + McpJsonQuote(_Name) +
        ",\"renderer\":" + McpJsonQuote(RendererTypeName(_Hook->GetRendererHookType())) +
        ",\"library\":" + McpJsonQuote(_Hook->GetLibraryName()) +
        ",\"hook_started\":" + (_Hook->IsStarted() ? "true" : "false") +
        ",\"hook_state\":\"" + hookState + "\"" +
        ",\"frame_count\":" + std::to_string(_FrameCount.load()) +
        "," + frameInfos + "}");
}

McpToolResult_t McpServerInternal_t::_ToolWaitFrames(McpJson_t const& arguments)
{
    auto count = arguments.Find("count");
    auto timeout = arguments.Find("timeout_ms");
    const uint64_t frames = count != nullptr && count->IsNumber() ? static_cast<uint64_t>(std::clamp(count->Number, 1.0, 10000.0)) : 1;
    const int timeoutMs = timeout != nullptr && timeout->IsNumber() ? static_cast<int>(std::clamp(timeout->Number, 1.0, 60000.0)) : 5000;

    const uint64_t target = _FrameCount.load() + frames;
    const auto expiration = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(_Mutex);
    _Condition.wait_until(lock, expiration, [&]() { return _FrameCount.load() >= target || _Stopping.load(); });

    const uint64_t current = _FrameCount.load();
    if (current < target)
        return TextResult("Timeout: " + std::to_string(frames - (target - current)) + " of " + std::to_string(frames) + " frames rendered.", true);

    return TextResult("{\"frame_count\":" + std::to_string(current) + "}");
}

McpToolResult_t McpServerInternal_t::_ToolScreenshot(McpJson_t const& arguments)
{
    auto includeOverlay = arguments.Find("include_overlay");
    auto maxDimension = arguments.Find("max_dimension");
    const auto type = includeOverlay != nullptr && includeOverlay->IsBool() && !includeOverlay->Bool ? ScreenshotType_t::BeforeOverlay : ScreenshotType_t::AfterOverlay;
    const uint32_t dimension = maxDimension != nullptr && maxDimension->IsNumber() ? static_cast<uint32_t>(std::clamp(maxDimension->Number, 16.0, 8192.0)) : DefaultMaxScreenshotDimension;

    auto pending = std::make_shared<McpPendingScreenshot_t>();
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        _Screenshot = pending;
    }

    std::string ignored;
    if (!_RunOnRenderThread([this, type]() { _Hook->TakeScreenshot(type); return std::string(); }, ignored))
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        _Screenshot.reset();
        return TextResult("The application doesn't render any frame.", true);
    }

    {
        std::unique_lock<std::mutex> lock(_Mutex);
        _Condition.wait_for(lock, ScreenshotTimeout, [&]() { return pending->Done || _Stopping.load(); });
        const bool done = pending->Done;
        _Screenshot.reset();
        if (!done)
            return TextResult("No screenshot received: the renderer hook did not deliver it.", true);
    }

    // Done on this thread, this must not slow down the rendering.
    uint32_t width, height;
    std::vector<uint8_t> rgb;
    if (!ConvertScreenshot(*pending, dimension, width, height, rgb))
        return TextResult("Unsupported screenshot format: " + std::to_string(static_cast<int>(pending->Format)), true);

    const std::string image = McpBase64(McpEncodePng(width, height, rgb));
    return McpToolResult_t{
        "[{\"type\":\"image\",\"mimeType\":\"image/png\",\"data\":\"" + image + "\"},"
        "{\"type\":\"text\",\"text\":" + McpJsonQuote(
            "{\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) +
            ",\"source_width\":" + std::to_string(pending->Width) + ",\"source_height\":" + std::to_string(pending->Height) + "}") + "}]",
        false };
}

McpToolResult_t McpServerInternal_t::_ToolMouse(McpJson_t const& arguments)
{
    auto x = arguments.Find("x");
    auto y = arguments.Find("y");
    auto action = arguments.Find("action");
    auto button = arguments.Find("button");
    if (x == nullptr || !x->IsNumber() || y == nullptr || !y->IsNumber())
        return TextResult("x and y are required.", true);

    const std::string actionName = action != nullptr && action->IsString() ? action->String : "move";
    if (actionName != "move" && actionName != "click" && actionName != "down" && actionName != "up")
        return TextResult("Unknown action: " + actionName, true);

    const int buttonIndex = button != nullptr && button->IsNumber() ? static_cast<int>(button->Number) : 0;
    if (buttonIndex < 0 || buttonIndex >= ImGuiMouseButton_COUNT)
        return TextResult("Unknown button.", true);

    const float posX = static_cast<float>(x->Number);
    const float posY = static_cast<float>(y->Number);
    return _RunTool([=]()
    {
        // ImGui trickles the queued events, so a click spans over several frames.
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(posX, posY);
        if (actionName == "click" || actionName == "down")
            io.AddMouseButtonEvent(buttonIndex, true);
        if (actionName == "click" || actionName == "up")
            io.AddMouseButtonEvent(buttonIndex, false);

        return std::string("{\"queued\":true}");
    });
}

McpToolResult_t McpServerInternal_t::_ToolKey(McpJson_t const& arguments)
{
    auto key = arguments.Find("key");
    auto action = arguments.Find("action");
    if (key == nullptr || !key->IsString())
        return TextResult("key is required.", true);

    const std::string keyName = key->String;
    const std::string actionName = action != nullptr && action->IsString() ? action->String : "press";
    if (actionName != "press" && actionName != "down" && actionName != "up")
        return TextResult("Unknown action: " + actionName, true);

    std::string result;
    if (!_RunOnRenderThread([=]()
    {
        for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; ++i)
        {
            const auto imguiKey = static_cast<ImGuiKey>(i);
            if (keyName != ImGui::GetKeyName(imguiKey))
                continue;

            auto& io = ImGui::GetIO();
            if (actionName == "press" || actionName == "down")
                io.AddKeyEvent(imguiKey, true);
            if (actionName == "press" || actionName == "up")
                io.AddKeyEvent(imguiKey, false);

            return std::string("{\"queued\":true}");
        }
        return std::string();
    }, result))
    {
        return TextResult("The application doesn't render any frame.", true);
    }

    if (result.empty())
        return TextResult("Unknown key: " + keyName, true);

    return TextResult(result);
}

McpToolResult_t McpServerInternal_t::_ToolInputPolicy(McpJson_t const& arguments)
{
    auto hideApp = arguments.Find("hide_from_app");
    auto hideOverlay = arguments.Find("hide_from_overlay");
    const int app = hideApp != nullptr && hideApp->IsBool() ? (hideApp->Bool ? 1 : 0) : -1;
    const int overlay = hideOverlay != nullptr && hideOverlay->IsBool() ? (hideOverlay->Bool ? 1 : 0) : -1;

    return _RunTool([=]()
    {
        if (app != -1)
            _Hook->HideAppInputs(app == 1);
        if (overlay != -1)
            _Hook->HideOverlayInputs(overlay == 1);

        return std::string("{\"applied\":true}");
    });
}

bool McpServerInternal_t::_WaitForFrames(uint64_t count, std::chrono::milliseconds timeout)
{
    const uint64_t target = _FrameCount.load() + count;
    std::unique_lock<std::mutex> lock(_Mutex);
    _Condition.wait_for(lock, timeout, [&]() { return _FrameCount.load() >= target || _Stopping.load(); });
    return _FrameCount.load() >= target;
}

#ifdef INGAMEOVERLAY_MCP_IMGUI_INSPECTOR
bool McpServerInternal_t::_CaptureSnapshot(McpImGuiSnapshot_t& snapshot, std::string& error)
{
    std::string result;
    if (!_RunOnRenderThread([]() { return McpImGuiArmCapture(); }, result))
    {
        error = NoFrameError;
        return false;
    }
    if (!result.empty())
    {
        error = result;
        return false;
    }

    // Each task runs at a later frame than the previous one: the recorded frame is complete after two of them.
    auto shared = std::make_shared<McpImGuiSnapshot_t>();
    for (int attempt = 0; attempt < MaxCaptureAttempts; ++attempt)
    {
        if (!_RunOnRenderThread([shared]()
        {
            std::string takeError;
            switch (McpImGuiTakeCapture(*shared, takeError))
            {
                case McpImGuiStatus_e::Done:    return std::string("done");
                case McpImGuiStatus_e::Pending: return std::string("pending");
                default:                        return "error:" + takeError;
            }
        }, result))
        {
            error = NoFrameError;
            return false;
        }

        if (result == "done")
        {
            snapshot = std::move(*shared);
            return true;
        }
        if (result != "pending")
        {
            error = result.substr(6);
            return false;
        }
    }

    error = "The capture didn't complete.";
    return false;
}

bool McpServerInternal_t::_CaptureWindows(McpImGuiSnapshot_t& snapshot)
{
    auto shared = std::make_shared<McpImGuiSnapshot_t>();
    std::string ignored;
    if (!_RunOnRenderThread([shared]() { McpImGuiTakeWindows(*shared); return std::string(); }, ignored))
        return false;

    snapshot = std::move(*shared);
    return true;
}

McpToolResult_t McpServerInternal_t::_ToolImGuiTree(McpJson_t const& arguments)
{
    auto window = arguments.Find("window");
    auto includeClipped = arguments.Find("include_clipped");
    auto includeInactive = arguments.Find("include_inactive");
    auto includeDecorations = arguments.Find("include_decorations");

    McpImGuiSnapshot_t snapshot;
    std::string error;
    if (!_CaptureSnapshot(snapshot, error))
        return TextResult(error, true);

    const std::string json = McpImGuiTreeJson(snapshot,
        window != nullptr && window->IsString() ? window->String : std::string(),
        includeClipped != nullptr && includeClipped->IsBool() && includeClipped->Bool,
        includeInactive != nullptr && includeInactive->IsBool() && includeInactive->Bool,
        includeDecorations != nullptr && includeDecorations->IsBool() && includeDecorations->Bool,
        error);

    if (json.empty())
        return TextResult(error, true);

    return TextResult(json);
}

McpToolResult_t McpServerInternal_t::_ToolImGuiFind(McpJson_t const& arguments)
{
    auto query = arguments.Find("query");
    auto type = arguments.Find("type");
    auto window = arguments.Find("window");
    auto limit = arguments.Find("limit");

    McpImGuiSnapshot_t snapshot;
    std::string error;
    if (!_CaptureSnapshot(snapshot, error))
        return TextResult(error, true);

    return TextResult(McpImGuiFindJson(snapshot,
        query != nullptr && query->IsString() ? query->String : std::string(),
        type != nullptr && type->IsString() ? type->String : std::string(),
        window != nullptr && window->IsString() ? window->String : std::string(),
        limit != nullptr && limit->IsNumber() ? static_cast<size_t>(std::clamp(limit->Number, 1.0, 1000.0)) : 50));
}

McpToolResult_t McpServerInternal_t::_ToolImGuiItem(McpJson_t const& arguments)
{
    auto actionArgument = arguments.Find("action");
    const std::string action = actionArgument != nullptr && actionArgument->IsString() ? actionArgument->String : "click";

    int button = 0;
    int clicks = 0;
    bool usesMouse = false;
    if (action == "click" || action == "double_click" || action == "right_click" || action == "hover")
    {
        usesMouse = true;
        button = action == "right_click" ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
        clicks = action == "hover" ? 0 : action == "double_click" ? 2 : 1;
    }
    else if (action != "info" && action != "activate" && action != "focus" && action != "scroll_to" && action != "check" && action != "uncheck" &&
        action != "open" && action != "close" && action != "set_value" && action != "set_text")
    {
        return TextResult("Unknown action: " + action, true);
    }

    double value = 0.0;
    if (action == "set_value")
    {
        auto valueArgument = arguments.Find("value");
        if (valueArgument == nullptr || !(valueArgument->IsNumber() || valueArgument->IsBool()))
            return TextResult("value (number or boolean) is required by set_value.", true);
        value = valueArgument->IsBool() ? (valueArgument->Bool ? 1.0 : 0.0) : valueArgument->Number;
    }

    std::string text;
    if (action == "set_text")
    {
        auto textArgument = arguments.Find("text");
        if (textArgument == nullptr || !textArgument->IsString())
            return TextResult("text is required by set_text.", true);
        text = textArgument->String;
    }

    auto clampArgument = arguments.Find("clamp");
    auto submitArgument = arguments.Find("submit");
    const bool clamp = clampArgument == nullptr || !clampArgument->IsBool() || clampArgument->Bool;
    const bool submit = submitArgument == nullptr || !submitArgument->IsBool() || submitArgument->Bool;

    McpImGuiSnapshot_t snapshot;
    std::string error;
    if (!_CaptureSnapshot(snapshot, error))
        return TextResult(error, true);

    auto found = McpImGuiResolveItem(snapshot, arguments, error);
    if (found == nullptr)
        return TextResult(error, true);

    McpImGuiItem_t target = *found;
    const std::string before = McpImGuiItemJson(snapshot, target, true);
    if (action == "info")
        return TextResult("{\"item\":" + before + "}");

    if (target.Disabled && action != "hover" && action != "focus" && action != "scroll_to")
        return TextResult("The item is disabled: " + before, true);

    // The mouse and the widget hooks only reach visible items.
    if (target.Clipped && (usesMouse || action == "set_value"))
    {
        const ImGuiID windowId = target.WindowId;
        const ImVec2 pos = target.Pos;
        const ImVec2 size = target.Size;
        if (!_RunOnRenderThread([=]() { return McpImGuiScrollToItem(windowId, pos, size); }, error))
            return TextResult(NoFrameError, true);
        if (!error.empty())
            return TextResult(error, true);

        _WaitForFrames(2, RenderThreadTimeout);
        if (!_CaptureSnapshot(snapshot, error))
            return TextResult(error, true);

        found = snapshot.FindItem(target.Id);
        if (found == nullptr || found->Clipped)
            return TextResult("The item can't be scrolled into view: " + before, true);
        target = *found;
    }

    std::string note;
    std::string written;
    uint64_t settleFrames = 3;
    const ImGuiID id = target.Id;
    const ImGuiID windowId = target.WindowId;

    auto runAction = [&](std::function<std::string()> function)
    {
        std::string result;
        if (!_RunOnRenderThread(std::move(function), result))
            return std::string(NoFrameError);
        return result;
    };

    if (usesMouse)
    {
        const ImVec2 center(target.Pos.x + target.Size.x * 0.5f, target.Pos.y + target.Size.y * 0.5f);
        error = runAction([=]() { return McpImGuiMouseOnItem(windowId, center, button, clicks); });
        settleFrames = 3 + 2 * clicks;
    }
    else if (action == "activate")
    {
        error = runAction([=]() { return McpImGuiActivateItem(windowId, id, false); });
    }
    else if (action == "check" || action == "uncheck")
    {
        if (!target.Checkable)
            return TextResult("The item isn't checkable: " + before, true);
        if (target.Checked == (action == "check"))
            note = "Already in this state.";
        else
            error = runAction([=]() { return McpImGuiActivateItem(windowId, id, false); });
    }
    else if (action == "open" || action == "close")
    {
        if (!target.Openable)
            return TextResult("The item can't be opened: " + before, true);
        if (target.Opened == (action == "open"))
            note = "Already in this state.";
        else
            error = runAction([=]() { return McpImGuiActivateItem(windowId, id, false); });
    }
    else if (action == "focus")
    {
        error = runAction([=]() { return McpImGuiFocusItem(windowId, id); });
    }
    else if (action == "scroll_to")
    {
        const ImVec2 pos = target.Pos;
        const ImVec2 size = target.Size;
        error = runAction([=]() { return McpImGuiScrollToItem(windowId, pos, size); });
    }
    else if (action == "set_value")
    {
        if (!target.Writable)
            return TextResult("The value of this item can't be written, try set_text or a click: " + before, true);

        error = runAction([=]() { return McpImGuiQueueWrite(id, value, clamp); });
        // The widget writes the value the next time it is submitted.
        bool done = !error.empty();
        for (int attempt = 0; attempt < MaxWriteAttempts && !done; ++attempt)
        {
            auto result = std::make_shared<std::pair<std::string, std::string>>();
            std::string status = runAction([=]()
            {
                switch (McpImGuiTakeWriteResult(id, result->first, result->second))
                {
                    case McpImGuiStatus_e::Done:    return std::string("done");
                    case McpImGuiStatus_e::Pending: return std::string("pending");
                    default:                        return std::string("failed");
                }
            });

            if (status == "pending")
                continue;

            done = true;
            if (status == "done")
                written = result->first;
            else
                error = status == "failed" ? result->second : status;
        }

        if (!done)
        {
            runAction([=]() { McpImGuiCancelWrite(id); return std::string(); });
            error = "The widget hasn't been submitted again: its window may be hidden or collapsed.";
        }
    }
    else if (action == "set_text")
    {
        // Like Ctrl+Click on a slider: text inputs are activated as is, sliders and drags switch to their text mode.
        error = runAction([=]() { return McpImGuiActivateItem(windowId, id, true); });
        bool typed = !error.empty();
        for (int attempt = 0; attempt < MaxTextInputAttempts && !typed; ++attempt)
        {
            std::string result = runAction([=]() { return McpImGuiReplaceText(id, text, submit); });
            if (result == McpImGuiNotActive)
                continue;

            typed = true;
            error = result;
        }

        if (!typed)
            error = "The item didn't become an active text input, it may not accept text: " + before;
        // One frame per typed event, at most.
        settleFrames = 4 + std::min<uint64_t>(text.size(), 64);
    }

    if (!error.empty())
        return TextResult(error, true);

    _WaitForFrames(settleFrames, RenderThreadTimeout);

    std::string after = "null";
    if (_CaptureSnapshot(snapshot, error))
    {
        if (auto item = snapshot.FindItem(id))
            after = McpImGuiItemJson(snapshot, *item, true);
    }

    std::string json = "{\"action\":\"" + action + "\",\"item\":" + before + ",\"after\":" + after;
    if (!written.empty())
        json += ",\"written\":" + written;
    if (!note.empty())
        json += ",\"note\":" + McpJsonQuote(note);
    return TextResult(json + "}");
}

McpToolResult_t McpServerInternal_t::_ToolImGuiWindow(McpJson_t const& arguments)
{
    auto nameArgument = arguments.Find("name");
    auto actionArgument = arguments.Find("action");
    if (nameArgument == nullptr || !nameArgument->IsString())
        return TextResult("name is required.", true);

    const std::string action = actionArgument != nullptr && actionArgument->IsString() ? actionArgument->String : "info";

    McpImGuiSnapshot_t snapshot;
    if (!_CaptureWindows(snapshot))
        return TextResult(NoFrameError, true);

    std::string error;
    auto window = McpImGuiResolveWindow(snapshot, nameArgument->String, error);
    if (window == nullptr)
        return TextResult(error, true);

    const std::string before = McpImGuiWindowJson(*window);
    if (action == "info")
        return TextResult("{\"window\":" + before + "}");

    const ImGuiID windowId = window->Id;
    const McpJson_t argumentsCopy = arguments;
    std::string result;
    if (!_RunOnRenderThread([=]() { return McpImGuiWindowAction(windowId, action, argumentsCopy); }, result))
        return TextResult(NoFrameError, true);
    if (!result.empty())
        return TextResult(result, true);

    _WaitForFrames(action == "close" ? 4 : 2, RenderThreadTimeout);

    std::string after = "null";
    if (_CaptureWindows(snapshot))
    {
        if (auto changed = snapshot.FindWindow(windowId))
            after = McpImGuiWindowJson(*changed);
    }

    return TextResult("{\"action\":\"" + action + "\",\"window\":" + before + ",\"after\":" + after + "}");
}

McpToolResult_t McpServerInternal_t::_ToolImGuiDebugLog(McpJson_t const& arguments)
{
    auto names = [&](char const* key)
    {
        std::vector<std::string> result;
        if (auto list = arguments.Find(key))
        {
            for (auto const& entry : list->Array)
            {
                if (entry.IsString())
                    result.push_back(entry.String);
            }
        }
        return result;
    };

    auto clearArgument = arguments.Find("clear");
    auto maxBytesArgument = arguments.Find("max_bytes");
    const std::vector<std::string> enable = names("enable");
    const std::vector<std::string> disable = names("disable");
    const bool clear = clearArgument != nullptr && clearArgument->IsBool() && clearArgument->Bool;
    const size_t maxBytes = maxBytesArgument != nullptr && maxBytesArgument->IsNumber() ? static_cast<size_t>(std::clamp(maxBytesArgument->Number, 256.0, 1048576.0)) : 16384;

    auto error = std::make_shared<std::string>();
    std::string result;
    if (!_RunOnRenderThread([=]() { return McpImGuiDebugLog(enable, disable, clear, maxBytes, *error); }, result))
        return TextResult(NoFrameError, true);

    // The error is written before the task is marked as done, under the mutex.
    if (result.empty())
        return TextResult(*error, true);

    return TextResult(result);
}

#endif

McpToolResult_t McpServerInternal_t::_CallTool(std::string const& name, McpJson_t const& arguments)
{
    #ifdef INGAMEOVERLAY_MCP_IMGUI_INSPECTOR
    if (name == "imgui_tree")
        return _ToolImGuiTree(arguments);

    if (name == "imgui_find")
        return _ToolImGuiFind(arguments);

    if (name == "imgui_item")
        return _ToolImGuiItem(arguments);

    if (name == "imgui_window")
        return _ToolImGuiWindow(arguments);

    if (name == "imgui_state")
        return _RunTool([]() { return McpImGuiStateJson(); });

    if (name == "imgui_debug_log")
        return _ToolImGuiDebugLog(arguments);

    #endif

    if (name == "overlay_status")
        return _ToolStatus();

    if (name == "overlay_wait_frames")
        return _ToolWaitFrames(arguments);

    if (name == "overlay_screenshot")
        return _ToolScreenshot(arguments);

    if (name == "overlay_mouse")
        return _ToolMouse(arguments);

    if (name == "overlay_key")
        return _ToolKey(arguments);

    if (name == "overlay_input_policy")
        return _ToolInputPolicy(arguments);

    if (name == "overlay_windows")
    {
        return _RunTool([]()
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            std::string windows = "[";
            for (ImGuiWindow const* window : g.Windows)
            {
                if (windows.size() != 1)
                    windows += ',';

                windows += "{\"name\":" + McpJsonQuote(window->Name) +
                    ",\"x\":" + McpJsonFloat(window->Pos.x) +
                    ",\"y\":" + McpJsonFloat(window->Pos.y) +
                    ",\"width\":" + McpJsonFloat(window->Size.x) +
                    ",\"height\":" + McpJsonFloat(window->Size.y) +
                    ",\"active\":" + (window->WasActive ? "true" : "false") +
                    ",\"focused\":" + (g.NavWindow == window ? "true" : "false") + "}";
            }
            return windows + "]";
        });
    }

    if (name == "overlay_text")
    {
        auto text = arguments.Find("text");
        if (text == nullptr || !text->IsString())
            return TextResult("text is required.", true);

        const std::string value = text->String;
        return _RunTool([value]()
        {
            ImGui::GetIO().AddInputCharactersUTF8(value.c_str());
            return std::string("{\"queued\":true}");
        });
    }

    if (name == "overlay_set_visible")
    {
        auto visible = arguments.Find("visible");
        if (visible == nullptr || !visible->IsBool())
            return TextResult("visible is required.", true);

        if (_Callbacks.SetOverlayVisible == nullptr)
            return TextResult("This application doesn't support it.", true);

        const bool value = visible->Bool;
        return _RunTool([this, value]()
        {
            _Callbacks.SetOverlayVisible(value, _Callbacks.UserParameter);
            return std::string("{\"applied\":true}");
        });
    }

    return McpToolResult_t{ std::string(), true };
}

std::string McpServerInternal_t::_HandleRequest(McpJson_t const& request)
{
    auto rpcError = [&](int code, char const* message)
    {
        auto id = request.Find("id");
        return "{\"jsonrpc\":\"2.0\",\"id\":" + (id != nullptr ? id->Raw : std::string("null")) +
            ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":" + McpJsonQuote(message) + "}}";
    };

    auto version = request.Find("jsonrpc");
    auto method = request.Find("method");
    auto id = request.Find("id");

    // A response to nothing we have sent: we don't send any request.
    if (method == nullptr && id != nullptr && (request.Find("result") != nullptr || request.Find("error") != nullptr))
        return std::string();

    if (version == nullptr || !version->IsString() || version->String != "2.0" || method == nullptr || !method->IsString())
        return rpcError(-32600, "Invalid Request");

    // Notifications never get a response, this covers notifications/initialized and notifications/cancelled.
    if (id == nullptr)
        return std::string();

    if (!id->IsString() && !id->IsNumber())
        return rpcError(-32600, "Invalid Request");

    static const McpJson_t emptyObject = []()
    {
        McpJson_t object;
        object.Type = McpJson_t::Type_e::Object;
        return object;
    }();

    auto params = request.Find("params");
    if (params != nullptr && !params->IsObject())
        return rpcError(-32602, "Invalid params");
    if (params == nullptr)
        params = &emptyObject;

    auto success = [&](std::string const& result)
    {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id->Raw + ",\"result\":" + result + "}";
    };

    if (method->String == "ping")
        return success("{}");

    if (method->String == "initialize")
    {
        _Initialized = true;
        auto requested = params->Find("protocolVersion");
        std::string negotiated = ProtocolVersions[0];
        for (auto const& supported : ProtocolVersions)
        {
            if (requested != nullptr && requested->IsString() && requested->String == supported)
                negotiated = supported;
        }

        return success(
            "{\"protocolVersion\":\"" + negotiated + "\","
            "\"capabilities\":{\"tools\":{\"listChanged\":true}},"
            "\"serverInfo\":{\"name\":\"ingame_overlay\",\"version\":\"1.0.0\"}}");
    }

    if (method->String == "tools/list")
    {
        std::string tools;
        for (auto const& tool : Tools)
        {
            if (!tools.empty())
                tools += ',';

            tools += "{\"name\":\"" + std::string(tool.Name) + "\",\"description\":" + McpJsonQuote(tool.Description) + ",\"inputSchema\":" + tool.InputSchema + "}";
        }
        {
            std::lock_guard<std::mutex> lock(_ToolsMutex);
            for (auto const& tool : _Tools)
                tools += ",{\"name\":" + McpJsonQuote(tool->Name) + ",\"description\":" + McpJsonQuote(tool->Description) + ",\"inputSchema\":" + tool->Schema + "}";
        }
        return success("{\"tools\":[" + tools + "]}");
    }

    if (method->String == "tools/call")
    {
        auto name = params->Find("name");
        auto arguments = params->Find("arguments");
        if (name == nullptr || !name->IsString() || (arguments != nullptr && !arguments->IsObject()))
            return rpcError(-32602, "Invalid params");

        bool known = false;
        for (auto const& tool : Tools)
            known = known || name->String == tool.Name;

        std::shared_ptr<Tool_t> custom;
        {
            std::lock_guard<std::mutex> lock(_ToolsMutex);
            for (auto const& tool : _Tools)
                if (tool->Name == name->String) custom = tool;
        }
        if (custom)
        {
            auto result = std::make_shared<McpToolResult_t>(TextResult("Tool removed", true));
            std::string ignored;
            std::string args = arguments ? arguments->Raw : "{}";
            bool ran = _RunOnRenderThread([custom, result, args]()
            {
                std::lock_guard<std::recursive_mutex> lock(custom->Mutex);
                if (!custom->Removed)
                {
                    custom->Running = true;
                    McpToolResponse_t response(result.get(), [](void* state, const char* text, size_t size, bool error)
                    {
                        *static_cast<McpToolResult_t*>(state) = TextResult(text ? std::string(text, size) : std::string(), error);
                    });
                    try { custom->Call(args.data(), args.size(), &response, custom->UserParameter); }
                    catch (...) { *result = TextResult("Tool callback threw", true); }
                    custom->Running = false;
                }
                return std::string();
            }, ignored);
            if (!ran) return success("{\"content\":[],\"isError\":true}");
            return success("{\"content\":" + result->Content + ",\"isError\":" + (result->IsError ? "true" : "false") + "}");
        }
        if (!known)
            return rpcError(-32602, "Unknown tool");

        const McpToolResult_t result = _CallTool(name->String, arguments != nullptr ? *arguments : emptyObject);
        return success("{\"content\":" + result.Content + ",\"isError\":" + (result.IsError ? "true" : "false") + "}");
    }

    return rpcError(-32601, "Method not found");
}

std::string McpServerInternal_t::_HandleLine(std::string const& line)
{
    McpJson_t request;
    if (!McpJson_t::Parse(line, request))
        return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";

    if (!request.IsObject())
        return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}";

    return _HandleRequest(request);
}

void McpServerInternal_t::_ServeConnection(McpConnection_t& connection)
{
    {
        std::lock_guard<std::mutex> lock(_OutputMutex);
        _Initialized = false;
        _Output = [&connection](std::string const& message) { auto line = message + '\n'; connection.Write(line.data(), line.size()); };
    }
    // Clear the borrowed connection on every return.
    auto cleanup = std::shared_ptr<void>(nullptr, [this](void*) { std::lock_guard<std::mutex> lock(_OutputMutex); _Output = {}; _Initialized = false; });
    std::string line;
    char buffer[4096];
    while (!_Stopping.load())
    {
        const size_t count = connection.Read(buffer, sizeof(buffer));
        if (count == 0)
            return;

        size_t start = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (buffer[i] != '\n')
                continue;

            line.append(buffer + start, i - start);
            start = i + 1;

            if (line.size() > MaxMessageSize)
                return;

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!line.empty())
            {
                std::string response = _HandleLine(line);
                if (!response.empty())
                {
                    std::lock_guard<std::mutex> outputLock(_OutputMutex);
                    response += '\n';
                    if (!connection.Write(response.data(), response.size()))
                        return;
                }
            }
            line.clear();
        }

        line.append(buffer + start, count - start);
        if (line.size() > MaxMessageSize)
            return;
    }
}

void McpServerInternal_t::_RunServer()
{
    while (!_Stopping.load())
    {
        auto connection = _Listener->Accept();
        if (connection == nullptr)
            return;

        _ServeConnection(*connection);
    }
}

void McpServerInternal_t::_RunTransport()
{
    std::unique_lock<std::mutex> lock(_Mutex);
    while (true)
    {
        _InboxCondition.wait(lock, [this]() { return _Stopping.load() || !_Inbox.empty(); });
        if (_Stopping.load())
            return;

        std::string message = std::move(_Inbox.front());
        _Inbox.pop_front();
        lock.unlock();

        // Unlocked: the tools wait for the render thread, and Send may push the next message.
        std::string response = _HandleLine(message);
        if (!response.empty())
        {
            std::lock_guard<std::mutex> outputLock(_OutputMutex);
            _Transport.Send(response.data(), response.size(), _Transport.UserParameter);
        }

        lock.lock();
    }
}

bool McpServerInternal_t::_Start(RendererHook_t* rendererHook, McpServerCallbacks_t const& callbacks, std::string const& name, McpServerTransport_t const& transport)
{
    if (rendererHook == nullptr || _Thread.joinable())
        return false;

    if (transport.Send == nullptr)
    {
        if (!McpIsValidEndpointName(name))
            return false;

        McpEndpoint_t endpoint;
        endpoint.Pid = CurrentProcessId();
        endpoint.Name = name;
        _Listener = McpListen(endpoint);
        if (_Listener == nullptr)
            return false;
    }

    {
        // PushMessage reads it.
        std::lock_guard<std::mutex> lock(_Mutex);
        _Transport = transport;
    }
    _Name = name;
    _Hook = rendererHook;
    _Callbacks = callbacks;
    _Stopping = false;
    _Initialized = false;
    if (transport.Send)
        _Output = [transport](std::string const& message) { transport.Send(message.data(), message.size(), transport.UserParameter); };
    _Notifier = std::thread([this, generation = _Generation.load()]() mutable
    {
        while (!_Stopping)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto current = _Generation.load();
            if (current == generation) continue;
            generation = current;
            std::lock_guard<std::mutex> lock(_OutputMutex);
            if (_Initialized && _Output)
                _Output(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
        }
    });
    _Thread = std::thread([this]()
    {
        if (_Listener != nullptr)
            _RunServer();
        else
            _RunTransport();
    });
    return true;
}

McpServerInternal_t::~McpServerInternal_t()
{
    Stop();
}

bool McpServerInternal_t::Start(RendererHook_t* rendererHook, McpServerCallbacks_t callbacks, std::string const& name)
{
    return _Start(rendererHook, callbacks, name, McpServerTransport_t());
}

bool McpServerInternal_t::StartWithTransport(RendererHook_t* rendererHook, McpServerTransport_t transport, McpServerCallbacks_t callbacks)
{
    return transport.Send != nullptr && _Start(rendererHook, callbacks, std::string(), transport);
}

bool McpServerInternal_t::PushMessage(const char* message, size_t size)
{
    if (message == nullptr || size == 0 || size > MaxMessageSize)
        return false;

    {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Transport.Send == nullptr || _Stopping.load() || _Inbox.size() >= MaxPendingMessages)
            return false;

        _Inbox.emplace_back(message, size);
    }
    _InboxCondition.notify_one();
    return true;
}

void McpServerInternal_t::Stop()
{
    if (!_Thread.joinable())
        return;

    {
        std::lock_guard<std::mutex> lock(_Mutex);
        _Stopping = true;
    }
    _Condition.notify_all();
    _InboxCondition.notify_all();
    if (_Listener != nullptr)
        _Listener->Wake();
    _Thread.join();
    _Notifier.join();
    _Output = {};
    _Initialized = false;
    _Listener.reset();

    std::lock_guard<std::mutex> lock(_Mutex);
    _Tasks.clear();
    _Screenshot.reset();
    _Inbox.clear();
    _Transport = McpServerTransport_t();
    _Hook = nullptr;
}

std::string const& McpServerInternal_t::GetName() const
{
    return _Name;
}

void McpServerInternal_t::OnFrame()
{
    if (!_Thread.joinable())
        return;

    std::vector<std::shared_ptr<McpRenderTask_t>> tasks;
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        _FrameCount.fetch_add(1);
        tasks.swap(_Tasks);
    }

    for (auto& task : tasks)
    {
        std::string result = task->Function();

        std::lock_guard<std::mutex> lock(_Mutex);
        task->Result = std::move(result);
        task->Done = true;
    }

    // Also wakes up overlay_wait_frames.
    _Condition.notify_all();
}

void McpServerInternal_t::OnHookState(OverlayHookState state)
{
    _HookState.store(static_cast<int>(state));
}

bool McpServerInternal_t::OnScreenshot(ScreenshotCallbackParameter_t const* screenshot)
{
    std::lock_guard<std::mutex> lock(_Mutex);
    if (_Screenshot == nullptr || _Screenshot->Done)
        return false;

    auto& pending = *_Screenshot;
    pending.Width = screenshot->Width;
    pending.Height = screenshot->Height;
    pending.Pitch = screenshot->Pitch;
    pending.Format = screenshot->Format;

    auto source = static_cast<uint8_t const*>(screenshot->Data);
    pending.Pixels.assign(source, source + size_t(screenshot->Pitch) * screenshot->Height);
    pending.Done = true;

    _Condition.notify_all();
    return true;
}

}
