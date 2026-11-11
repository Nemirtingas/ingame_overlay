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

#include <imgui.h>
#include <imgui_internal.h>

namespace InGameOverlay {

// Receives the ImGui "test engine" hooks: every item submitted while ImGuiContext::TestEngineHookItems is set.
// Install it in ImGuiContext::TestEngine. The hooks run on the thread rendering the ImGui frame.
// They are only compiled in when the library is built with INGAMEOVERLAY_BUILD_MCP (IMGUI_ENABLE_TEST_ENGINE). ItemValue and ItemText
// are not part of upstream ImGui, they come from the Nemirtingas fork.
class McpImGuiHookListener_t
{
public:
    virtual void ItemAdd(ImGuiContext* context, ImGuiID id, ImRect const& bb, ImGuiLastItemData const* itemData) = 0;
    virtual void ItemInfo(ImGuiContext* context, ImGuiID id, char const* label, ImGuiItemStatusFlags flags) = 0;
    // Returns true after writing a new value at data, only allowed when writable.
    virtual bool ItemValue(ImGuiContext* context, ImGuiID id, char const* widget, ImGuiDataType dataType, void* data, int components, void const* min, void const* max, char const* format, bool writable) = 0;
    virtual void ItemText(ImGuiContext* context, ImRect const& bb, char const* label, char const* labelEnd, char const* text, char const* textEnd) = 0;
    virtual char const* FindItemLabel(ImGuiContext* context, ImGuiID id) = 0;

protected:
    ~McpImGuiHookListener_t() = default;
};

}
