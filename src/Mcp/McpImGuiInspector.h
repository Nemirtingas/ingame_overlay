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

#include "McpJson.h"

#include <imgui.h>

#include <string>
#include <vector>

// Reads and drives the ImGui UI for the MCP server. The items are recorded through the ImGui test engine hooks, and the widget
// values through the hooks added to the Nemirtingas ImGui fork (see McpImGuiHooks.h). Functions marked "render thread" must run
// inside the ImGui frame, from McpServerInternal_t::OnFrame.

namespace InGameOverlay {

struct McpImGuiWindow_t
{
    ImGuiID Id = 0;
    // The window containing a child window, 0 for the other windows.
    ImGuiID ParentId = 0;
    std::string Name;
    char const* Type = "window";
    ImVec2 Pos;
    ImVec2 Size;
    ImVec2 Scroll;
    ImVec2 ScrollMax;
    ImGuiWindowFlags Flags = 0;
    // Index in the display order, from back to front.
    int ZOrder = 0;
    bool Active = false;
    bool Collapsed = false;
    bool Focused = false;
    bool Hovered = false;
    bool Hidden = false;
    bool HasCloseButton = false;
};

// An item submitted during the captured frame: a widget, or a text without id (Type "Text").
struct McpImGuiItem_t
{
    ImGuiID Id = 0;
    ImGuiID WindowId = 0;
    // The enclosing item in the same window (a tree node...), 0 when there is none.
    ImGuiID ParentId = 0;
    // The widget name ("Button", "Slider", "Checkbox"...), "Text", a window decoration ("CloseButton", "CollapseButton", "TitleBar",
    // "ResizeGrip", "ResizeBorder", "Scrollbar") or "Item" when unknown.
    std::string Type;
    std::string Label;
    // JSON value, empty when the widget has none.
    std::string Value;
    // The value as displayed (scalars formatted with the widget format, texts).
    std::string Text;
    std::string DataType;
    std::string Min;
    std::string Max;
    ImVec2 Pos;
    ImVec2 Size;
    bool Decoration = false;
    bool Writable = false;
    bool Checkable = false;
    bool Checked = false;
    bool Openable = false;
    bool Opened = false;
    bool Selected = false;
    bool Clipped = false;
    bool Disabled = false;
    bool Hovered = false;
    bool Active = false;
    bool Focused = false;
};

struct McpImGuiSnapshot_t
{
    int FrameCount = 0;
    std::vector<McpImGuiWindow_t> Windows;
    std::vector<McpImGuiItem_t> Items;

    McpImGuiWindow_t const* FindWindow(ImGuiID id) const;
    McpImGuiItem_t const* FindItem(ImGuiID id) const;
    // Window name, then the labels of the enclosing items, without their "##" suffixes.
    std::string ItemPath(McpImGuiItem_t const& item) const;
};

enum class McpImGuiStatus_e { Done, Pending, Failed };

// Render thread. Starts recording the items. Returns an error message on failure.
std::string McpImGuiArmCapture();

// Render thread. Copies the items of the previous frame once it has entirely been recorded, then stops recording.
McpImGuiStatus_e McpImGuiTakeCapture(McpImGuiSnapshot_t& snapshot, std::string& error);

// Render thread. Copies the windows only, it doesn't need a capture.
void McpImGuiTakeWindows(McpImGuiSnapshot_t& snapshot);

// Render thread. The value is written by the widget itself the next time it is submitted, which then reports an edition.
std::string McpImGuiQueueWrite(ImGuiID id, double value, bool clamp);
// Render thread. Done with the written value (JSON), Pending until the widget has been submitted.
McpImGuiStatus_e McpImGuiTakeWriteResult(ImGuiID id, std::string& value, std::string& error);
// Render thread. Forgets a write the widget never received.
void McpImGuiCancelWrite(ImGuiID id);

// Render thread. The context state useful to debug inputs, focus and popups.
std::string McpImGuiStateJson();

// Render thread. Changes the categories of the ImGui debug log and returns its end.
std::string McpImGuiDebugLog(std::vector<std::string> const& enable, std::vector<std::string> const& disable, bool clear, size_t maxBytes, std::string& error);

// Render thread actions, they return an error message on failure.
std::string McpImGuiMouseOnItem(ImGuiID windowId, ImVec2 pos, int button, int clicks);
std::string McpImGuiActivateItem(ImGuiID windowId, ImGuiID id, bool preferTextInput);
std::string McpImGuiFocusItem(ImGuiID windowId, ImGuiID id);
std::string McpImGuiScrollToItem(ImGuiID windowId, ImVec2 pos, ImVec2 size);
// Returns McpImGuiNotActive while the item isn't being edited yet.
std::string McpImGuiReplaceText(ImGuiID id, std::string const& text, bool submit);
extern char const* const McpImGuiNotActive;
std::string McpImGuiWindowAction(ImGuiID windowId, std::string const& action, McpJson_t const& arguments);

// Any thread.
std::string McpImGuiFormatId(ImGuiID id);
bool McpImGuiParseId(McpJson_t const& value, ImGuiID& id);
std::string McpImGuiItemJson(McpImGuiSnapshot_t const& snapshot, McpImGuiItem_t const& item, bool withPath);
std::string McpImGuiWindowJson(McpImGuiWindow_t const& window);
// The windows matching the name (all of them if empty) with their child windows and their items as trees.
std::string McpImGuiTreeJson(McpImGuiSnapshot_t const& snapshot, std::string const& windowName, bool includeClipped, bool includeInactive, bool includeDecorations, std::string& error);
// The items whose label or text contains the query (case insensitive), as a flat list. Window decorations only when looked for by type.
std::string McpImGuiFindJson(McpImGuiSnapshot_t const& snapshot, std::string const& query, std::string const& type, std::string const& windowName, size_t limit);
// Finds the window by name, the name without its "##" suffix is accepted too.
McpImGuiWindow_t const* McpImGuiResolveWindow(McpImGuiSnapshot_t const& snapshot, std::string const& name, std::string& error);
// Finds the item designated by the "id" or "label" (and optional "window") arguments.
McpImGuiItem_t const* McpImGuiResolveItem(McpImGuiSnapshot_t const& snapshot, McpJson_t const& arguments, std::string& error);

}
