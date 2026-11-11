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

// Compiled into ingame_overlay, next to imgui.cpp, when the MCP server is built: imgui.cpp references these functions as soon as
// IMGUI_ENABLE_TEST_ENGINE is defined. They do nothing until the MCP server installs a listener in ImGuiContext::TestEngine.

#include "McpImGuiHooks.h"

using InGameOverlay::McpImGuiHookListener_t;

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data)
{
    if (auto listener = static_cast<McpImGuiHookListener_t*>(ctx->TestEngine))
        listener->ItemAdd(ctx, id, bb, item_data);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    if (auto listener = static_cast<McpImGuiHookListener_t*>(ctx->TestEngine))
        listener->ItemInfo(ctx, id, label, flags);
}

bool ImGuiTestEngineHook_ItemValue(ImGuiContext* ctx, ImGuiID id, const char* widget, ImGuiDataType data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format, bool writable)
{
    if (auto listener = static_cast<McpImGuiHookListener_t*>(ctx->TestEngine))
        return listener->ItemValue(ctx, id, widget, data_type, p_data, components, p_min, p_max, format, writable);

    return false;
}

void ImGuiTestEngineHook_ItemText(ImGuiContext* ctx, const ImRect& bb, const char* label, const char* label_end, const char* text, const char* text_end)
{
    if (auto listener = static_cast<McpImGuiHookListener_t*>(ctx->TestEngine))
        listener->ItemText(ctx, bb, label, label_end, text, text_end);
}

// Only reached with ImGuiDebugLogFlags_OutputToTestEngine, the debug log is already in ImGuiContext::DebugLogBuf.
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
}

// Used by the ID stack tool of the metrics window.
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id)
{
    if (auto listener = static_cast<McpImGuiHookListener_t*>(ctx->TestEngine))
        return listener->FindItemLabel(ctx, id);

    return nullptr;
}
