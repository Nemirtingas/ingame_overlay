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

#include "McpImGuiInspector.h"
#include "McpImGuiHooks.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace InGameOverlay {

char const* const McpImGuiNotActive = "not_active";

namespace {

constexpr size_t MaxTextSize = 4096;
constexpr int NotArmed = std::numeric_limits<int>::min();
// Far more than the server waits for a capture or a write.
constexpr int MaxRequestFrames = 600;

struct CapturedItem_t
{
    ImGuiID Id = 0;
    ImGuiID WindowId = 0;
    ImGuiID ParentId = 0;
    ImRect Rect;
    ImGuiItemFlags ItemFlags = 0;
    ImGuiItemStatusFlags InfoFlags = 0;
    bool IsText = false;
    bool Clipped = false;
    bool Writable = false;
    std::string Label;
    std::string Widget;
    std::string Value;
    std::string Text;
    std::string DataType;
    std::string Min;
    std::string Max;
};

struct CapturedFrame_t
{
    int FrameCount = -1;
    std::vector<CapturedItem_t> Items;
    std::unordered_map<ImGuiID, size_t> Index;

    void Reset(int frameCount)
    {
        FrameCount = frameCount;
        Items.clear();
        Index.clear();
    }
};

struct PendingWrite_t
{
    double Value = 0.0;
    bool Clamp = true;
    int Frame = 0;
};

struct WriteResult_t
{
    bool Applied = false;
    std::string Value;
    std::string Error;
};

void Truncate(std::string& text, size_t maxSize)
{
    if (text.size() <= maxSize)
        return;

    // Don't cut a UTF-8 sequence.
    size_t size = maxSize;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
        --size;

    text.resize(size);
    text += "...";
}

std::string DisplayName(std::string const& label)
{
    const size_t pos = label.find("##");
    return pos == std::string::npos ? label : label.substr(0, pos);
}

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return text;
}

char const* DataTypeName(ImGuiDataType dataType)
{
    switch (dataType)
    {
        case ImGuiDataType_S8:     return "s8";
        case ImGuiDataType_U8:     return "u8";
        case ImGuiDataType_S16:    return "s16";
        case ImGuiDataType_U16:    return "u16";
        case ImGuiDataType_S32:    return "s32";
        case ImGuiDataType_U32:    return "u32";
        case ImGuiDataType_S64:    return "s64";
        case ImGuiDataType_U64:    return "u64";
        case ImGuiDataType_Float:  return "float";
        case ImGuiDataType_Double: return "double";
        case ImGuiDataType_Bool:   return "bool";
        case ImGuiDataType_String: return "string";
        default:                   return "";
    }
}

bool IsScalar(ImGuiDataType dataType)
{
    return dataType >= ImGuiDataType_S8 && dataType <= ImGuiDataType_Double;
}

template<typename T>
T Load(void const* data)
{
    T value;
    memcpy(&value, data, sizeof(T));
    return value;
}

template<typename T>
void Store(void* data, double value)
{
    T result;
    if constexpr (std::is_integral<T>::value)
    {
        value = std::round(value);
        if (!(value > static_cast<double>(std::numeric_limits<T>::lowest())))
            result = std::numeric_limits<T>::lowest();
        else if (value >= static_cast<double>(std::numeric_limits<T>::max()))
            result = std::numeric_limits<T>::max();
        else
            result = static_cast<T>(value);
    }
    else
    {
        result = static_cast<T>(value);
    }
    memcpy(data, &result, sizeof(T));
}

double ScalarToDouble(ImGuiDataType dataType, void const* data)
{
    switch (dataType)
    {
        case ImGuiDataType_S8:     return Load<ImS8>(data);
        case ImGuiDataType_U8:     return Load<ImU8>(data);
        case ImGuiDataType_S16:    return Load<ImS16>(data);
        case ImGuiDataType_U16:    return Load<ImU16>(data);
        case ImGuiDataType_S32:    return Load<ImS32>(data);
        case ImGuiDataType_U32:    return Load<ImU32>(data);
        case ImGuiDataType_S64:    return static_cast<double>(Load<ImS64>(data));
        case ImGuiDataType_U64:    return static_cast<double>(Load<ImU64>(data));
        case ImGuiDataType_Float:  return Load<float>(data);
        case ImGuiDataType_Double: return Load<double>(data);
        default:                   return 0.0;
    }
}

void DoubleToScalar(ImGuiDataType dataType, void* data, double value)
{
    switch (dataType)
    {
        case ImGuiDataType_S8:     Store<ImS8>(data, value);   break;
        case ImGuiDataType_U8:     Store<ImU8>(data, value);   break;
        case ImGuiDataType_S16:    Store<ImS16>(data, value);  break;
        case ImGuiDataType_U16:    Store<ImU16>(data, value);  break;
        case ImGuiDataType_S32:    Store<ImS32>(data, value);  break;
        case ImGuiDataType_U32:    Store<ImU32>(data, value);  break;
        case ImGuiDataType_S64:    Store<ImS64>(data, value);  break;
        case ImGuiDataType_U64:    Store<ImU64>(data, value);  break;
        case ImGuiDataType_Float:  Store<float>(data, value);  break;
        case ImGuiDataType_Double: Store<double>(data, value); break;
        default: break;
    }
}

// 64-bit integers are not converted through a double, to keep their precision.
std::string ScalarJson(ImGuiDataType dataType, void const* data)
{
    switch (dataType)
    {
        case ImGuiDataType_S8:     return std::to_string(Load<ImS8>(data));
        case ImGuiDataType_U8:     return std::to_string(Load<ImU8>(data));
        case ImGuiDataType_S16:    return std::to_string(Load<ImS16>(data));
        case ImGuiDataType_U16:    return std::to_string(Load<ImU16>(data));
        case ImGuiDataType_S32:    return std::to_string(Load<ImS32>(data));
        case ImGuiDataType_U32:    return std::to_string(Load<ImU32>(data));
        case ImGuiDataType_S64:    return std::to_string(Load<ImS64>(data));
        case ImGuiDataType_U64:    return std::to_string(Load<ImU64>(data));
        case ImGuiDataType_Float:  return McpJsonDouble(Load<float>(data));
        case ImGuiDataType_Double: return McpJsonDouble(Load<double>(data));
        default:                   return "null";
    }
}

size_t ScalarSize(ImGuiDataType dataType)
{
    return IsScalar(dataType) ? ImGui::DataTypeGetInfo(dataType)->Size : 0;
}

void ReadValue(CapturedItem_t& item, ImGuiDataType dataType, void const* data, int components, void const* min, void const* max, char const* format)
{
    item.DataType = DataTypeName(dataType);
    item.Value.clear();
    item.Text.clear();
    item.Min.clear();
    item.Max.clear();
    if (data == nullptr)
        return;

    if (dataType == ImGuiDataType_Bool)
    {
        item.Value = *static_cast<bool const*>(data) ? "true" : "false";
    }
    else if (dataType == ImGuiDataType_String)
    {
        std::string text(static_cast<char const*>(data));
        Truncate(text, MaxTextSize);
        item.Value = McpJsonQuote(text);
        item.Text = std::move(text);
    }
    else if (IsScalar(dataType))
    {
        const size_t size = ScalarSize(dataType);
        if (components > 1)
        {
            item.Value = "[";
            for (int i = 0; i < components; ++i)
            {
                if (i != 0)
                    item.Value += ',';
                item.Value += ScalarJson(dataType, static_cast<char const*>(data) + i * size);
            }
            item.Value += ']';
        }
        else
        {
            item.Value = ScalarJson(dataType, data);

            // What the user reads, with the format of the widget.
            char buffer[64];
            ImGui::DataTypeFormatString(buffer, IM_COUNTOF(buffer), dataType, data, format != nullptr ? format : ImGui::DataTypeGetInfo(dataType)->PrintFmt);
            item.Text = buffer;
        }

        if (min != nullptr)
            item.Min = ScalarJson(dataType, min);
        if (max != nullptr)
            item.Max = ScalarJson(dataType, max);
    }
}

class Collector_t final : public McpImGuiHookListener_t
{
public:
    ImGuiContext* Context = nullptr;
    int ArmFrame = NotArmed;
    CapturedFrame_t Frames[2];
    int Current = 0;
    std::unordered_map<ImGuiID, PendingWrite_t> Writes;
    std::unordered_map<ImGuiID, WriteResult_t> WriteResults;

    // Installs the listener in the context, returns an error message on failure.
    std::string Attach(ImGuiContext* context)
    {
        McpImGuiHookListener_t* self = this;
        if (context->TestEngine != nullptr && context->TestEngine != self)
            return "Another ImGui test engine is installed.";

        if (Context != context)
        {
            Context = context;
            ArmFrame = NotArmed;
            Frames[0].Reset(-1);
            Frames[1].Reset(-1);
            Writes.clear();
            WriteResults.clear();
        }
        context->TestEngine = self;
        return std::string();
    }

    void UpdateHooks()
    {
        if (Context != nullptr)
            Context->TestEngineHookItems = ArmFrame != NotArmed || !Writes.empty();
    }

    CapturedFrame_t& Frame(ImGuiContext* context)
    {
        if (Frames[Current].FrameCount != context->FrameCount)
        {
            Current ^= 1;
            Frames[Current].Reset(context->FrameCount);
            ExpireRequests(context->FrameCount);
        }
        return Frames[Current];
    }

    // A server stopped in the middle of a request must not leave the hooks recording every frame.
    void ExpireRequests(int frameCount)
    {
        if (ArmFrame != NotArmed && frameCount - ArmFrame > MaxRequestFrames)
            ArmFrame = NotArmed;

        for (auto it = Writes.begin(); it != Writes.end();)
        {
            if (frameCount - it->second.Frame > MaxRequestFrames)
                it = Writes.erase(it);
            else
                ++it;
        }
        UpdateHooks();
    }

    CapturedFrame_t const* FindFrame(int frameCount) const
    {
        for (auto const& frame : Frames)
        {
            if (frame.FrameCount == frameCount)
                return &frame;
        }
        return nullptr;
    }

    static CapturedItem_t* Find(CapturedFrame_t& frame, ImGuiID id)
    {
        if (!frame.Items.empty() && frame.Items.back().Id == id)
            return &frame.Items.back();

        auto it = frame.Index.find(id);
        return it == frame.Index.end() ? nullptr : &frame.Items[it->second];
    }

    // The closest item of the ID stack, TreeNode() and BeginMenu() push theirs.
    static ImGuiID FindParent(CapturedFrame_t const& frame, ImGuiWindow const* window)
    {
        for (int i = window->IDStack.Size - 1; i >= 0; --i)
        {
            auto it = frame.Index.find(window->IDStack[i]);
            if (it != frame.Index.end() && frame.Items[it->second].WindowId == window->ID)
                return it->first;
        }
        return 0;
    }

    void ItemAdd(ImGuiContext* context, ImGuiID id, ImRect const& bb, ImGuiLastItemData const* itemData) override
    {
        ImGuiWindow* window = context->CurrentWindow;
        // Begin() registers the window itself.
        if (window == nullptr || id == window->ID)
            return;

        auto& frame = Frame(context);
        // ButtonBehavior() registers again the items which didn't go through ItemAdd().
        if (frame.Index.find(id) != frame.Index.end())
            return;

        CapturedItem_t item;
        item.Id = id;
        item.WindowId = window->ID;
        item.ParentId = FindParent(frame, window);
        item.Rect = bb;
        item.ItemFlags = itemData != nullptr ? itemData->ItemFlags : context->CurrentItemFlags;
        item.Clipped = !bb.Overlaps(window->ClipRect);

        frame.Index.emplace(id, frame.Items.size());
        frame.Items.emplace_back(std::move(item));
    }

    void ItemInfo(ImGuiContext* context, ImGuiID id, char const* label, ImGuiItemStatusFlags flags) override
    {
        if (CapturedItem_t* item = Find(Frame(context), id))
        {
            if (label != nullptr)
                item->Label = label;
            item->InfoFlags |= flags;
        }
    }

    bool ItemValue(ImGuiContext* context, ImGuiID id, char const* widget, ImGuiDataType dataType, void* data, int components, void const* min, void const* max, char const* format, bool writable) override
    {
        CapturedItem_t* item = Find(Frame(context), id);
        const bool canWrite = writable && data != nullptr && components == 1 && (IsScalar(dataType) || dataType == ImGuiDataType_Bool);
        if (item != nullptr)
        {
            item->Widget = widget;
            item->Writable = canWrite;
            ReadValue(*item, dataType, data, components, min, max, format);
        }

        auto pending = Writes.find(id);
        if (pending == Writes.end() || !canWrite)
            return false;

        WriteResult_t result;
        if ((context->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0)
        {
            result.Error = "The item is disabled.";
        }
        else if (dataType == ImGuiDataType_Bool)
        {
            *static_cast<bool*>(data) = pending->second.Value != 0.0;
            result.Applied = true;
        }
        else
        {
            double value = pending->second.Value;
            if (pending->second.Clamp && min != nullptr && max != nullptr)
            {
                // A slider may have min > max, a drag without bounds has min == max.
                const double low = std::min(ScalarToDouble(dataType, min), ScalarToDouble(dataType, max));
                const double high = std::max(ScalarToDouble(dataType, min), ScalarToDouble(dataType, max));
                if (low < high)
                    value = std::clamp(value, low, high);
            }
            DoubleToScalar(dataType, data, value);
            result.Applied = true;
        }

        if (result.Applied)
        {
            result.Value = dataType == ImGuiDataType_Bool ? (*static_cast<bool*>(data) ? "true" : "false") : ScalarJson(dataType, data);
            if (item != nullptr)
                ReadValue(*item, dataType, data, components, min, max, format);
        }

        Writes.erase(pending);
        WriteResults[id] = std::move(result);
        UpdateHooks();
        return WriteResults[id].Applied;
    }

    void ItemText(ImGuiContext* context, ImRect const& bb, char const* label, char const* labelEnd, char const* text, char const* textEnd) override
    {
        ImGuiWindow* window = context->CurrentWindow;
        if (window == nullptr || text == nullptr)
            return;

        auto& frame = Frame(context);
        CapturedItem_t item;
        item.IsText = true;
        item.WindowId = window->ID;
        item.ParentId = FindParent(frame, window);
        item.Rect = bb;
        item.ItemFlags = context->CurrentItemFlags;
        item.Clipped = !bb.Overlaps(window->ClipRect);
        if (label != nullptr)
            item.Label.assign(label, labelEnd != nullptr ? labelEnd : label + strlen(label));
        item.Text.assign(text, textEnd != nullptr ? textEnd : text + strlen(text));
        Truncate(item.Text, MaxTextSize);

        frame.Items.emplace_back(std::move(item));
    }

    char const* FindItemLabel(ImGuiContext*, ImGuiID id) override
    {
        for (int i = 0; i < 2; ++i)
        {
            auto const& frame = Frames[Current ^ i];
            auto it = frame.Index.find(id);
            if (it != frame.Index.end() && !frame.Items[it->second].Label.empty())
                return frame.Items[it->second].Label.c_str();
        }
        return nullptr;
    }
};

// Never destroyed before the ImGui context: both live in the module the overlay is linked into.
Collector_t& GetCollector()
{
    static Collector_t collector;
    return collector;
}

char const* WindowType(ImGuiWindowFlags flags)
{
    if (flags & ImGuiWindowFlags_Tooltip)
        return "tooltip";
    if (flags & ImGuiWindowFlags_Modal)
        return "modal";
    if (flags & ImGuiWindowFlags_ChildMenu)
        return "menu";
    if (flags & ImGuiWindowFlags_Popup)
        return "popup";
    if (flags & ImGuiWindowFlags_ChildWindow)
        return "child";
    return "window";
}

struct FlagName_t
{
    int Flag;
    char const* Name;
};

constexpr FlagName_t WindowFlagNames[] = {
    { ImGuiWindowFlags_NoTitleBar, "NoTitleBar" },
    { ImGuiWindowFlags_NoResize, "NoResize" },
    { ImGuiWindowFlags_NoMove, "NoMove" },
    { ImGuiWindowFlags_NoScrollbar, "NoScrollbar" },
    { ImGuiWindowFlags_NoScrollWithMouse, "NoScrollWithMouse" },
    { ImGuiWindowFlags_NoCollapse, "NoCollapse" },
    { ImGuiWindowFlags_AlwaysAutoResize, "AlwaysAutoResize" },
    { ImGuiWindowFlags_NoBackground, "NoBackground" },
    { ImGuiWindowFlags_NoSavedSettings, "NoSavedSettings" },
    { ImGuiWindowFlags_NoMouseInputs, "NoMouseInputs" },
    { ImGuiWindowFlags_MenuBar, "MenuBar" },
    { ImGuiWindowFlags_HorizontalScrollbar, "HorizontalScrollbar" },
    { ImGuiWindowFlags_NoFocusOnAppearing, "NoFocusOnAppearing" },
    { ImGuiWindowFlags_NoBringToFrontOnFocus, "NoBringToFrontOnFocus" },
    { ImGuiWindowFlags_AlwaysVerticalScrollbar, "AlwaysVerticalScrollbar" },
    { ImGuiWindowFlags_AlwaysHorizontalScrollbar, "AlwaysHorizontalScrollbar" },
    { ImGuiWindowFlags_NoNavInputs, "NoNavInputs" },
    { ImGuiWindowFlags_NoNavFocus, "NoNavFocus" },
    { ImGuiWindowFlags_UnsavedDocument, "UnsavedDocument" },
};

constexpr FlagName_t DebugLogCategories[] = {
    { ImGuiDebugLogFlags_EventError, "error" },
    { ImGuiDebugLogFlags_EventActiveId, "activeid" },
    { ImGuiDebugLogFlags_EventFocus, "focus" },
    { ImGuiDebugLogFlags_EventPopup, "popup" },
    { ImGuiDebugLogFlags_EventNav, "nav" },
    { ImGuiDebugLogFlags_EventClipper, "clipper" },
    { ImGuiDebugLogFlags_EventSelection, "selection" },
    { ImGuiDebugLogFlags_EventIO, "io" },
    { ImGuiDebugLogFlags_EventFont, "font" },
    { ImGuiDebugLogFlags_EventInputRouting, "inputrouting" },
    { ImGuiDebugLogFlags_EventTable, "table" },
};

constexpr FlagName_t ConfigFlagNames[] = {
    { ImGuiConfigFlags_NavEnableKeyboard, "NavEnableKeyboard" },
    { ImGuiConfigFlags_NavEnableGamepad, "NavEnableGamepad" },
    { ImGuiConfigFlags_NoMouse, "NoMouse" },
    { ImGuiConfigFlags_NoMouseCursorChange, "NoMouseCursorChange" },
    { ImGuiConfigFlags_NoKeyboard, "NoKeyboard" },
};

std::string BoolJson(bool value)
{
    return value ? "true" : "false";
}

std::string Vec2Json(ImVec2 value)
{
    return "[" + McpJsonFloat(value.x) + "," + McpJsonFloat(value.y) + "]";
}

std::string RectJson(ImVec2 pos, ImVec2 size)
{
    return "[" + McpJsonFloat(pos.x) + "," + McpJsonFloat(pos.y) + "," + McpJsonFloat(size.x) + "," + McpJsonFloat(size.y) + "]";
}

std::string NamesJson(std::vector<char const*> const& names)
{
    std::string json = "[";
    for (auto name : names)
    {
        if (json.size() != 1)
            json += ',';
        json += '"';
        json += name;
        json += '"';
    }
    return json + "]";
}

std::string WindowNameJson(ImGuiWindow const* window)
{
    return window != nullptr ? McpJsonQuote(window->Name) : std::string("null");
}

std::string IdJson(ImGuiID id)
{
    if (id == 0)
        return "null";

    std::string json = "{\"id\":\"" + McpImGuiFormatId(id) + "\"";
    if (char const* label = GetCollector().FindItemLabel(nullptr, id))
        json += ",\"label\":" + McpJsonQuote(label);
    return json + "}";
}

ImGuiWindow* FindLiveWindow(ImGuiID windowId, std::string& error)
{
    ImGuiWindow* window = ImGui::FindWindowByID(windowId);
    if (window == nullptr)
        error = "The window doesn't exist anymore.";
    return window;
}

// Like a click would, but a popup is already in front and focusing another window would close it.
void BringToFront(ImGuiWindow* window)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* root = window->RootWindow;
    if (root->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip))
        return;

    if (g.NavWindow != nullptr && g.NavWindow->RootWindow == root)
        return;

    ImGui::FocusWindow(window);
}

bool WindowMatches(McpImGuiSnapshot_t const& snapshot, ImGuiID windowId, std::string const& name)
{
    // A child window belongs to its parents.
    for (McpImGuiWindow_t const* window = snapshot.FindWindow(windowId); window != nullptr; window = window->ParentId != 0 ? snapshot.FindWindow(window->ParentId) : nullptr)
    {
        if (window->Name == name || DisplayName(window->Name) == name)
            return true;
    }
    return false;
}

std::string WindowFields(McpImGuiWindow_t const& window)
{
    std::vector<char const*> state;
    if (!window.Active)
        state.push_back("inactive");
    if (window.Collapsed)
        state.push_back("collapsed");
    if (window.Focused)
        state.push_back("focused");
    if (window.Hovered)
        state.push_back("hovered");
    if (window.Hidden)
        state.push_back("hidden");

    std::vector<char const*> flags;
    for (auto const& flag : WindowFlagNames)
    {
        if (window.Flags & flag.Flag)
            flags.push_back(flag.Name);
    }

    std::string json = "\"name\":" + McpJsonQuote(window.Name) +
        ",\"id\":\"" + McpImGuiFormatId(window.Id) + "\"" +
        ",\"type\":\"" + window.Type + "\"" +
        ",\"rect\":" + RectJson(window.Pos, window.Size) +
        ",\"z\":" + std::to_string(window.ZOrder) +
        ",\"state\":" + NamesJson(state) +
        ",\"flags\":" + NamesJson(flags) +
        ",\"has_close_button\":" + BoolJson(window.HasCloseButton);

    if (window.ScrollMax.x > 0.0f || window.ScrollMax.y > 0.0f)
        json += ",\"scroll\":" + Vec2Json(window.Scroll) + ",\"scroll_max\":" + Vec2Json(window.ScrollMax);

    return json;
}

std::string ItemFields(McpImGuiSnapshot_t const& snapshot, McpImGuiItem_t const& item, bool withPath)
{
    std::string json = "\"type\":" + McpJsonQuote(item.Type);
    if (item.Id != 0)
        json += ",\"id\":\"" + McpImGuiFormatId(item.Id) + "\"";
    if (!item.Label.empty())
        json += ",\"label\":" + McpJsonQuote(item.Label);
    if (!item.Value.empty())
        json += ",\"value\":" + item.Value;
    // Only when it says something more than the value.
    if (!item.Text.empty() && (item.Value.empty() || item.DataType != "string"))
        json += ",\"text\":" + McpJsonQuote(item.Text);
    if (!item.DataType.empty())
        json += ",\"data_type\":\"" + item.DataType + "\"";
    if (!item.Min.empty())
        json += ",\"min\":" + item.Min;
    if (!item.Max.empty())
        json += ",\"max\":" + item.Max;

    json += ",\"rect\":" + RectJson(item.Pos, item.Size);

    std::vector<char const*> state;
    if (item.Checkable && item.Checked)
        state.push_back("checked");
    if (item.Openable && item.Opened)
        state.push_back("open");
    if (item.Selected)
        state.push_back("selected");
    if (item.Writable)
        state.push_back("writable");
    if (item.Disabled)
        state.push_back("disabled");
    if (item.Hovered)
        state.push_back("hovered");
    if (item.Active)
        state.push_back("active");
    if (item.Focused)
        state.push_back("focused");
    if (item.Clipped)
        state.push_back("clipped");
    if (!state.empty())
        json += ",\"state\":" + NamesJson(state);

    if (withPath)
    {
        auto window = snapshot.FindWindow(item.WindowId);
        json += ",\"window\":" + (window != nullptr ? McpJsonQuote(window->Name) : std::string("null"));
        json += ",\"path\":" + McpJsonQuote(snapshot.ItemPath(item));
    }
    return json;
}

// The items Begin() submits for the window itself.
char const* DecorationType(ImGuiWindow* window, ImGuiID id)
{
    if (id == ImHashStr("#CLOSE", 0, window->ID))
        return "CloseButton";
    if (id == ImHashStr("#COLLAPSE", 0, window->ID))
        return "CollapseButton";
    if (id == window->MoveId)
        return "TitleBar";
    for (int i = 0; i < 4; ++i)
    {
        if (id == ImGui::GetWindowResizeCornerID(window, i))
            return "ResizeGrip";
        if (id == ImGui::GetWindowResizeBorderID(window, static_cast<ImGuiDir>(i)))
            return "ResizeBorder";
    }
    if (id == ImGui::GetWindowScrollbarID(window, ImGuiAxis_X) || id == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y))
        return "Scrollbar";
    return nullptr;
}

uint64_t ParentKey(ImGuiID windowId, ImGuiID parentId)
{
    return (static_cast<uint64_t>(windowId) << 32) | parentId;
}

}

McpImGuiWindow_t const* McpImGuiSnapshot_t::FindWindow(ImGuiID id) const
{
    for (auto const& window : Windows)
    {
        if (window.Id == id)
            return &window;
    }
    return nullptr;
}

McpImGuiItem_t const* McpImGuiSnapshot_t::FindItem(ImGuiID id) const
{
    if (id == 0)
        return nullptr;

    for (auto const& item : Items)
    {
        if (item.Id == id)
            return &item;
    }
    return nullptr;
}

std::string McpImGuiSnapshot_t::ItemPath(McpImGuiItem_t const& item) const
{
    auto itemName = [](McpImGuiItem_t const& value)
    {
        std::string name = DisplayName(value.Label);
        if (name.empty())
            name = value.Type == "Text" ? value.Text : value.Type;
        return name;
    };

    std::string path = itemName(item);
    // The parents were submitted before their children, so this ends.
    for (auto parent = FindItem(item.ParentId); parent != nullptr; parent = FindItem(parent->ParentId))
        path = itemName(*parent) + "/" + path;

    auto window = FindWindow(item.WindowId);
    return (window != nullptr ? DisplayName(window->Name) : std::string("?")) + "/" + path;
}

std::string McpImGuiArmCapture()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr)
        return "There is no ImGui context.";

    auto& collector = GetCollector();
    std::string error = collector.Attach(context);
    if (!error.empty())
        return error;

    collector.ArmFrame = context->FrameCount;
    collector.UpdateHooks();
    return std::string();
}

void McpImGuiTakeWindows(McpImGuiSnapshot_t& snapshot)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // At the beginning of a frame, the windows still describe the previous one.
    snapshot.FrameCount = g.FrameCount - 1;
    snapshot.Windows.clear();
    for (int i = 0; i < g.Windows.Size; ++i)
    {
        ImGuiWindow const* window = g.Windows[i];
        if (window->IsFallbackWindow)
            continue;

        McpImGuiWindow_t result;
        result.Id = window->ID;
        result.Name = window->Name;
        result.Type = WindowType(window->Flags);
        if (strcmp(result.Type, "child") == 0 && window->ParentWindow != nullptr)
            result.ParentId = window->ParentWindow->ID;
        result.Pos = window->Pos;
        result.Size = window->Size;
        result.Scroll = window->Scroll;
        result.ScrollMax = window->ScrollMax;
        result.Flags = window->Flags;
        result.ZOrder = i;
        result.Active = window->WasActive;
        result.Collapsed = window->Collapsed;
        result.Focused = g.NavWindow == window;
        result.Hovered = g.HoveredWindow == window;
        result.Hidden = window->Hidden;
        result.HasCloseButton = window->HasCloseButton;
        snapshot.Windows.emplace_back(std::move(result));
    }
}

McpImGuiStatus_e McpImGuiTakeCapture(McpImGuiSnapshot_t& snapshot, std::string& error)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    auto& collector = GetCollector();
    if (context == nullptr || collector.Context != context || collector.ArmFrame == NotArmed)
    {
        error = "The ImGui context has changed during the capture, try again.";
        return McpImGuiStatus_e::Failed;
    }

    ImGuiContext& g = *context;
    // The frame we read must have been entirely recorded.
    const int frameCount = g.FrameCount - 1;
    if (frameCount <= collector.ArmFrame)
        return McpImGuiStatus_e::Pending;

    collector.ArmFrame = NotArmed;
    collector.UpdateHooks();

    McpImGuiTakeWindows(snapshot);
    snapshot.Items.clear();

    // No frame means that nothing has been submitted.
    CapturedFrame_t const* frame = collector.FindFrame(frameCount);
    if (frame == nullptr)
        return McpImGuiStatus_e::Done;

    std::unordered_map<ImGuiID, bool> tabs;
    for (int i = 0; i < g.TabBars.GetMapSize(); ++i)
    {
        if (ImGuiTabBar* tabBar = g.TabBars.TryGetMapData(i))
        {
            for (auto const& tab : tabBar->Tabs)
                tabs[tab.ID] = tabBar->SelectedTabId == tab.ID;
        }
    }

    snapshot.Items.reserve(frame->Items.size());
    for (auto const& captured : frame->Items)
    {
        McpImGuiItem_t item;
        item.Id = captured.Id;
        item.WindowId = captured.WindowId;
        item.ParentId = captured.ParentId;
        item.Label = captured.Label;
        item.Value = captured.Value;
        item.Text = captured.Text;
        item.DataType = captured.DataType;
        item.Min = captured.Min;
        item.Max = captured.Max;
        item.Pos = captured.Rect.Min;
        item.Size = captured.Rect.GetSize();
        item.Clipped = captured.Clipped;
        item.Disabled = (captured.ItemFlags & ImGuiItemFlags_Disabled) != 0;

        if (captured.IsText)
        {
            item.Type = "Text";
            snapshot.Items.emplace_back(std::move(item));
            continue;
        }

        ImGuiWindow* window = ImGui::FindWindowByID(captured.WindowId);
        char const* decoration = window != nullptr ? DecorationType(window, captured.Id) : nullptr;
        if (decoration != nullptr)
            item.Type = decoration;
        else if (!captured.Widget.empty())
            item.Type = captured.Widget;
        else
            item.Type = "Item";
        item.Decoration = decoration != nullptr;

        item.Writable = captured.Writable && !item.Disabled;
        item.Checkable = (captured.InfoFlags & ImGuiItemStatusFlags_Checkable) != 0;
        item.Checked = (captured.InfoFlags & ImGuiItemStatusFlags_Checked) != 0;
        item.Openable = (captured.InfoFlags & ImGuiItemStatusFlags_Openable) != 0;
        item.Opened = (captured.InfoFlags & ImGuiItemStatusFlags_Opened) != 0;

        auto tab = tabs.find(captured.Id);
        if (tab != tabs.end())
            item.Selected = tab->second;
        else if (item.Type == "Selectable")
            item.Selected = item.Value == "true";

        item.Hovered = g.HoveredIdPreviousFrame == captured.Id;
        item.Active = g.ActiveId == captured.Id;
        item.Focused = g.NavId == captured.Id;
        snapshot.Items.emplace_back(std::move(item));
    }
    return McpImGuiStatus_e::Done;
}

std::string McpImGuiQueueWrite(ImGuiID id, double value, bool clamp)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr)
        return "There is no ImGui context.";

    if (!std::isfinite(value))
        return "The value must be finite.";

    auto& collector = GetCollector();
    std::string error = collector.Attach(context);
    if (!error.empty())
        return error;

    collector.WriteResults.erase(id);
    collector.Writes[id] = PendingWrite_t{ value, clamp, context->FrameCount };
    collector.UpdateHooks();
    return std::string();
}

McpImGuiStatus_e McpImGuiTakeWriteResult(ImGuiID id, std::string& value, std::string& error)
{
    auto& collector = GetCollector();
    auto it = collector.WriteResults.find(id);
    if (it == collector.WriteResults.end())
    {
        if (collector.Writes.find(id) != collector.Writes.end())
            return McpImGuiStatus_e::Pending;

        error = "The write has been lost, the ImGui context may have changed.";
        return McpImGuiStatus_e::Failed;
    }

    const bool applied = it->second.Applied;
    value = std::move(it->second.Value);
    error = std::move(it->second.Error);
    collector.WriteResults.erase(it);
    return applied ? McpImGuiStatus_e::Done : McpImGuiStatus_e::Failed;
}

void McpImGuiCancelWrite(ImGuiID id)
{
    auto& collector = GetCollector();
    collector.Writes.erase(id);
    collector.WriteResults.erase(id);
    collector.UpdateHooks();
}

std::string McpImGuiStateJson()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = g.IO;

    std::string mouseDown = "[";
    for (int i = 0; i < ImGuiMouseButton_COUNT; ++i)
    {
        if (io.MouseDown[i])
            mouseDown += (mouseDown.size() == 1 ? "" : ",") + std::to_string(i);
    }
    mouseDown += "]";

    std::string keysDown = "[";
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
    {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key)))
            keysDown += (keysDown.size() == 1 ? "" : ",") + McpJsonQuote(ImGui::GetKeyName(static_cast<ImGuiKey>(key)));
    }
    keysDown += "]";

    std::vector<char const*> configFlags;
    for (auto const& flag : ConfigFlagNames)
    {
        if (io.ConfigFlags & flag.Flag)
            configFlags.push_back(flag.Name);
    }

    std::string popups = "[";
    for (auto const& popup : g.OpenPopupStack)
    {
        if (popups.size() != 1)
            popups += ',';
        popups += "{\"id\":\"" + McpImGuiFormatId(popup.PopupId) + "\",\"window\":" + WindowNameJson(popup.Window) +
            ",\"open_frame\":" + std::to_string(popup.OpenFrameCount) + "}";
    }
    popups += "]";

    std::string inputText = "null";
    if (g.InputTextState.ID != 0 && g.InputTextState.ID == g.ActiveId)
    {
        auto& state = g.InputTextState;
        inputText = "{\"id\":\"" + McpImGuiFormatId(state.ID) + "\"";
        if ((state.Flags & ImGuiInputTextFlags_Password) == 0)
        {
            std::string text(state.TextA.Data != nullptr ? state.TextA.Data : "", state.TextA.Data != nullptr ? state.TextLen : 0);
            Truncate(text, MaxTextSize);
            inputText += ",\"text\":" + McpJsonQuote(text);
        }
        inputText += ",\"cursor\":" + std::to_string(state.GetCursorPos()) +
            ",\"selection\":[" + std::to_string(state.GetSelectionStart()) + "," + std::to_string(state.GetSelectionEnd()) + "]" +
            ",\"multiline\":" + BoolJson((state.Flags & ImGuiInputTextFlags_Multiline) != 0) +
            ",\"read_only\":" + BoolJson((state.Flags & ImGuiInputTextFlags_ReadOnly) != 0) + "}";
    }

    std::string dragDrop = "null";
    if (g.DragDropActive)
        dragDrop = "{\"payload_type\":" + McpJsonQuote(g.DragDropPayload.DataType) + ",\"source\":" + IdJson(g.DragDropPayload.SourceId) + "}";

    return std::string("{\"frame_count\":") + std::to_string(g.FrameCount) +
        ",\"time\":" + McpJsonDouble(g.Time) +
        ",\"framerate\":" + McpJsonFloat(io.Framerate) +
        ",\"display_size\":" + Vec2Json(io.DisplaySize) +
        ",\"framebuffer_scale\":" + Vec2Json(io.DisplayFramebufferScale) +
        ",\"font_size\":" + McpJsonFloat(g.FontSize) +
        ",\"config_flags\":" + NamesJson(configFlags) +
        ",\"mouse\":{\"pos\":" + Vec2Json(io.MousePos) + ",\"down\":" + mouseDown + ",\"cursor\":" + std::to_string(g.MouseCursor) + "}" +
        ",\"keys_down\":" + keysDown +
        ",\"want_capture_mouse\":" + BoolJson(io.WantCaptureMouse) +
        ",\"want_capture_keyboard\":" + BoolJson(io.WantCaptureKeyboard) +
        ",\"want_text_input\":" + BoolJson(io.WantTextInput) +
        ",\"hovered_window\":" + WindowNameJson(g.HoveredWindow) +
        ",\"hovered_item\":" + IdJson(g.HoveredIdPreviousFrame) +
        ",\"focused_window\":" + WindowNameJson(g.NavWindow) +
        ",\"nav_item\":" + IdJson(g.NavId) +
        ",\"nav_cursor_visible\":" + BoolJson(g.NavCursorVisible) +
        ",\"active_item\":" + IdJson(g.ActiveId) +
        ",\"active_window\":" + WindowNameJson(g.ActiveIdWindow) +
        ",\"moving_window\":" + WindowNameJson(g.MovingWindow) +
        ",\"open_popups\":" + popups +
        ",\"input_text\":" + inputText +
        ",\"drag_drop\":" + dragDrop +
        ",\"metrics\":{\"windows\":" + std::to_string(g.Windows.Size) +
            ",\"active_windows\":" + std::to_string(io.MetricsActiveWindows) +
            ",\"render_windows\":" + std::to_string(io.MetricsRenderWindows) +
            ",\"vertices\":" + std::to_string(io.MetricsRenderVertices) +
            ",\"indices\":" + std::to_string(io.MetricsRenderIndices) + "}" +
        "}";
}

std::string McpImGuiDebugLog(std::vector<std::string> const& enable, std::vector<std::string> const& disable, bool clear, size_t maxBytes, std::string& error)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();

    auto findCategory = [&](std::string const& name, ImGuiDebugLogFlags& flag)
    {
        for (auto const& category : DebugLogCategories)
        {
            if (name == category.Name)
            {
                flag = category.Flag;
                return true;
            }
        }

        error = "Unknown category: " + name + ". Known categories:";
        for (auto const& category : DebugLogCategories)
            error += std::string(" ") + category.Name;
        return false;
    };

    ImGuiDebugLogFlags enabled = 0;
    ImGuiDebugLogFlags disabled = 0;
    for (auto const& name : enable)
    {
        ImGuiDebugLogFlags flag;
        if (!findCategory(name, flag))
            return std::string();
        enabled |= flag;
    }
    for (auto const& name : disable)
    {
        ImGuiDebugLogFlags flag;
        if (!findCategory(name, flag))
            return std::string();
        disabled |= flag;
    }

    g.DebugLogFlags = (g.DebugLogFlags | enabled) & ~disabled;
    if (clear)
    {
        g.DebugLogBuf.clear();
        g.DebugLogIndex.clear();
    }

    std::vector<char const*> categories;
    for (auto const& category : DebugLogCategories)
    {
        if (g.DebugLogFlags & category.Flag)
            categories.push_back(category.Name);
    }

    // The end of the log, from the beginning of a line.
    std::string log(g.DebugLogBuf.begin(), g.DebugLogBuf.end());
    bool truncated = false;
    if (log.size() > maxBytes)
    {
        size_t start = log.size() - maxBytes;
        const size_t newLine = log.find('\n', start);
        start = newLine == std::string::npos ? start : newLine + 1;
        log.erase(0, start);
        truncated = true;
    }

    return "{\"enabled\":" + NamesJson(categories) +
        ",\"skipped_errors\":" + std::to_string(g.DebugLogSkippedErrors) +
        ",\"truncated\":" + BoolJson(truncated) +
        ",\"log\":" + McpJsonQuote(log) + "}";
}

std::string McpImGuiMouseOnItem(ImGuiID windowId, ImVec2 pos, int button, int clicks)
{
    std::string error;
    ImGuiWindow* window = FindLiveWindow(windowId, error);
    if (window == nullptr)
        return error;

    BringToFront(window);

    // ImGui trickles the queued events, so the clicks span over several frames.
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(pos.x, pos.y);
    for (int i = 0; i < clicks; ++i)
    {
        io.AddMouseButtonEvent(button, true);
        io.AddMouseButtonEvent(button, false);
    }
    return std::string();
}

std::string McpImGuiActivateItem(ImGuiID windowId, ImGuiID id, bool preferTextInput)
{
    std::string error;
    if (FindLiveWindow(windowId, error) == nullptr)
        return error;

    // Processed by the next NewFrame(), then by the widget when it is submitted again: like pressing Space on it.
    ImGui::ActivateItemByID(id);
    if (preferTextInput)
        ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    return std::string();
}

std::string McpImGuiFocusItem(ImGuiID windowId, ImGuiID id)
{
    std::string error;
    ImGuiWindow* window = FindLiveWindow(windowId, error);
    if (window == nullptr)
        return error;

    ImGui::FocusWindow(window);
    ImGui::SetFocusID(id, window);
    ImGui::GetCurrentContext()->NavCursorVisible = true;
    return std::string();
}

std::string McpImGuiScrollToItem(ImGuiID windowId, ImVec2 pos, ImVec2 size)
{
    std::string error;
    ImGuiWindow* window = FindLiveWindow(windowId, error);
    if (window == nullptr)
        return error;

    ImGui::ScrollToRect(window, ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)));
    return std::string();
}

std::string McpImGuiReplaceText(ImGuiID id, std::string const& text, bool submit)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiInputTextState* state = ImGui::GetInputTextState(id);
    if (g.ActiveId != id || state == nullptr)
        return McpImGuiNotActive;

    if (state->Flags & ImGuiInputTextFlags_ReadOnly)
        return "The input is read-only.";

    // The typed text replaces the selection.
    state->SelectAll();
    auto& io = g.IO;
    if (text.empty())
    {
        io.AddKeyEvent(ImGuiKey_Delete, true);
        io.AddKeyEvent(ImGuiKey_Delete, false);
    }
    else
    {
        io.AddInputCharactersUTF8(text.c_str());
    }

    // Enter would add a new line to a multiline input, which applies the edits as they are typed anyway.
    if (submit && (state->Flags & ImGuiInputTextFlags_Multiline) == 0)
    {
        io.AddKeyEvent(ImGuiKey_Enter, true);
        io.AddKeyEvent(ImGuiKey_Enter, false);
    }
    return std::string();
}

std::string McpImGuiWindowAction(ImGuiID windowId, std::string const& action, McpJson_t const& arguments)
{
    std::string error;
    ImGuiWindow* window = FindLiveWindow(windowId, error);
    if (window == nullptr)
        return error;

    auto number = [&](char const* name, float& value)
    {
        auto argument = arguments.Find(name);
        if (argument == nullptr || !argument->IsNumber())
            return false;
        value = static_cast<float>(argument->Number);
        return true;
    };

    if (action == "focus")
    {
        ImGui::FocusWindow(window);
    }
    else if (action == "collapse" || action == "expand")
    {
        ImGui::SetWindowCollapsed(window, action == "collapse");
    }
    else if (action == "move")
    {
        ImVec2 pos;
        if (!number("x", pos.x) || !number("y", pos.y))
            return "x and y are required to move a window.";
        ImGui::SetWindowPos(window, pos);
    }
    else if (action == "resize")
    {
        ImVec2 size;
        if (!number("width", size.x) || !number("height", size.y))
            return "width and height are required to resize a window.";
        ImGui::SetWindowSize(window, size);
    }
    else if (action == "scroll")
    {
        float x, y;
        const bool hasX = number("x", x);
        const bool hasY = number("y", y);
        if (!hasX && !hasY)
            return "x or y is required to scroll a window.";
        if (hasX)
            ImGui::SetScrollX(window, x);
        if (hasY)
            ImGui::SetScrollY(window, y);
    }
    else if (action == "close")
    {
        if (!window->HasCloseButton)
            return "This window has no close button, only the application can close it.";
        ImGui::ActivateItemByID(ImHashStr("#CLOSE", 0, window->ID));
    }
    else
    {
        return "Unknown action: " + action;
    }
    return std::string();
}

std::string McpImGuiFormatId(ImGuiID id)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(id));
    return buffer;
}

bool McpImGuiParseId(McpJson_t const& value, ImGuiID& id)
{
    if (value.IsNumber())
    {
        if (value.Number < 1.0 || value.Number > 4294967295.0)
            return false;
        id = static_cast<ImGuiID>(value.Number);
        return true;
    }

    if (!value.IsString() || value.String.empty())
        return false;

    char* end = nullptr;
    const unsigned long long parsed = strtoull(value.String.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || parsed == 0 || parsed > 0xFFFFFFFFull)
        return false;

    id = static_cast<ImGuiID>(parsed);
    return true;
}

std::string McpImGuiItemJson(McpImGuiSnapshot_t const& snapshot, McpImGuiItem_t const& item, bool withPath)
{
    return "{" + ItemFields(snapshot, item, withPath) + "}";
}

std::string McpImGuiWindowJson(McpImGuiWindow_t const& window)
{
    return "{" + WindowFields(window) + "}";
}

std::string McpImGuiTreeJson(McpImGuiSnapshot_t const& snapshot, std::string const& windowName, bool includeClipped, bool includeInactive, bool includeDecorations, std::string& error)
{
    std::vector<McpImGuiWindow_t const*> roots;
    if (!windowName.empty())
    {
        auto window = McpImGuiResolveWindow(snapshot, windowName, error);
        if (window == nullptr)
            return std::string();
        roots.push_back(window);
    }
    else
    {
        for (auto const& window : snapshot.Windows)
        {
            if (window.ParentId == 0 && (window.Active || includeInactive))
                roots.push_back(&window);
        }
    }

    std::unordered_map<ImGuiID, std::vector<McpImGuiWindow_t const*>> childWindows;
    for (auto const& window : snapshot.Windows)
    {
        if (window.ParentId != 0 && (window.Active || includeInactive))
            childWindows[window.ParentId].push_back(&window);
    }

    std::unordered_map<uint64_t, std::vector<McpImGuiItem_t const*>> children;
    for (auto const& item : snapshot.Items)
        children[ParentKey(item.WindowId, item.ParentId)].push_back(&item);

    std::string json;
    // Skipped items give their children to their parent. The depth is bounded by the nesting of the UI.
    std::function<void(ImGuiID, ImGuiID, bool&)> writeItems = [&](ImGuiID windowId, ImGuiID parentId, bool& first)
    {
        auto it = children.find(ParentKey(windowId, parentId));
        if (it == children.end())
            return;

        for (auto item : it->second)
        {
            if ((item->Clipped && !includeClipped) || (item->Decoration && !includeDecorations))
            {
                if (item->Id != 0)
                    writeItems(windowId, item->Id, first);
                continue;
            }

            if (!first)
                json += ',';
            first = false;

            json += '{' + ItemFields(snapshot, *item, false);
            if (item->Id != 0 && children.find(ParentKey(windowId, item->Id)) != children.end())
            {
                json += ",\"children\":[";
                bool firstChild = true;
                writeItems(windowId, item->Id, firstChild);
                json += ']';
            }
            json += '}';
        }
    };

    std::function<void(McpImGuiWindow_t const&)> writeWindow = [&](McpImGuiWindow_t const& window)
    {
        json += '{' + WindowFields(window) + ",\"items\":[";
        bool first = true;
        writeItems(window.Id, 0, first);
        json += ']';

        auto it = childWindows.find(window.Id);
        if (it != childWindows.end())
        {
            json += ",\"child_windows\":[";
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                if (i != 0)
                    json += ',';
                writeWindow(*it->second[i]);
            }
            json += ']';
        }
        json += '}';
    };

    json = "{\"frame\":" + std::to_string(snapshot.FrameCount) + ",\"windows\":[";
    for (size_t i = 0; i < roots.size(); ++i)
    {
        if (i != 0)
            json += ',';
        writeWindow(*roots[i]);
    }
    return json + "]}";
}

std::string McpImGuiFindJson(McpImGuiSnapshot_t const& snapshot, std::string const& query, std::string const& type, std::string const& windowName, size_t limit)
{
    const std::string lowerQuery = ToLower(query);
    const std::string lowerType = ToLower(type);

    std::string items;
    size_t count = 0;
    for (auto const& item : snapshot.Items)
    {
        // Decorations only when asked for by type.
        if (lowerType.empty() ? item.Decoration : ToLower(item.Type) != lowerType)
            continue;
        if (!windowName.empty() && !WindowMatches(snapshot, item.WindowId, windowName))
            continue;
        if (!lowerQuery.empty() && ToLower(item.Label).find(lowerQuery) == std::string::npos && ToLower(item.Text).find(lowerQuery) == std::string::npos)
            continue;

        if (++count > limit)
            continue;

        if (!items.empty())
            items += ',';
        items += McpImGuiItemJson(snapshot, item, true);
    }

    return "{\"frame\":" + std::to_string(snapshot.FrameCount) + ",\"count\":" + std::to_string(count) +
        ",\"truncated\":" + BoolJson(count > limit) + ",\"items\":[" + items + "]}";
}

McpImGuiWindow_t const* McpImGuiResolveWindow(McpImGuiSnapshot_t const& snapshot, std::string const& name, std::string& error)
{
    for (auto const& window : snapshot.Windows)
    {
        if (window.Name == name)
            return &window;
    }

    McpImGuiWindow_t const* found = nullptr;
    for (auto const& window : snapshot.Windows)
    {
        if (DisplayName(window.Name) != name)
            continue;

        if (found != nullptr)
        {
            error = "Several windows are named \"" + name + "\", use the full name: \"" + found->Name + "\", \"" + window.Name + "\".";
            return nullptr;
        }
        found = &window;
    }

    if (found == nullptr)
    {
        error = "No window named \"" + name + "\". Active windows:";
        for (auto const& window : snapshot.Windows)
        {
            if (window.Active)
                error += " \"" + window.Name + "\"";
        }
    }
    return found;
}

McpImGuiItem_t const* McpImGuiResolveItem(McpImGuiSnapshot_t const& snapshot, McpJson_t const& arguments, std::string& error)
{
    auto idArgument = arguments.Find("id");
    auto labelArgument = arguments.Find("label");
    auto windowArgument = arguments.Find("window");

    if (idArgument != nullptr)
    {
        ImGuiID id;
        if (!McpImGuiParseId(*idArgument, id))
        {
            error = "Invalid id, use the \"0x...\" value given by imgui_tree or imgui_find.";
            return nullptr;
        }

        auto item = snapshot.FindItem(id);
        if (item == nullptr)
            error = "No item " + McpImGuiFormatId(id) + " in the last frame: its window may be closed or collapsed, or it may be scrolled out of a list clipper.";
        return item;
    }

    if (labelArgument == nullptr || !labelArgument->IsString() || labelArgument->String.empty())
    {
        error = "id or label is required.";
        return nullptr;
    }

    const std::string& label = labelArgument->String;
    const std::string windowName = windowArgument != nullptr && windowArgument->IsString() ? windowArgument->String : std::string();

    std::vector<McpImGuiItem_t const*> matches;
    for (int pass = 0; pass < 2 && matches.empty(); ++pass)
    {
        for (auto const& item : snapshot.Items)
        {
            if (item.Id == 0)
                continue;
            if (!windowName.empty() && !WindowMatches(snapshot, item.WindowId, windowName))
                continue;

            const bool match = pass == 0
                ? item.Label == label || DisplayName(item.Label) == label
                : ToLower(DisplayName(item.Label)) == ToLower(label);
            if (match)
                matches.push_back(&item);
        }
    }

    if (matches.size() == 1)
        return matches[0];

    if (matches.empty())
    {
        error = "No item labelled \"" + label + "\"" + (windowName.empty() ? std::string() : " in window \"" + windowName + "\"") + ", use imgui_find to search.";
        return nullptr;
    }

    error = "Several items are labelled \"" + label + "\", give their id or a window:";
    for (size_t i = 0; i < matches.size() && i < 10; ++i)
        error += " " + McpImGuiFormatId(matches[i]->Id) + " (" + snapshot.ItemPath(*matches[i]) + ")";
    return nullptr;
}

}
