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
#include <string>
#include <memory>
#include <cstdint>
#include <set>

namespace ingame_overlay {

enum class ToggleKey
{
    SHIFT, CTRL, ALT,
    TAB,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

class Renderer_Hook
{
public:
    Renderer_Hook():
        OverlayProc(&DefaultOverlayProc),
        OverlayHookReady(&DefaultOverlayHookReady)
    {}

    static void DefaultOverlayProc() {}
    static void DefaultOverlayHookReady(bool) {}
    std::function<void()> OverlayProc;
    std::function<void(bool)> OverlayHookReady;

    /// <summary>
    ///   Starts the current renderer hook procedure, allowing a user to render things on the application window.
    /// </summary>
    /// <param name="key_combination_callback">
    ///   key_combination_callback is called with parameter "false" when the library wants to know if it should block inputs.
    ///   key_combination_callback is called with parameter "true" when the library detected the "toggle_keys" has been pressed.
    ///   You should return false when you don't want to block inputs, true otherwise.
    /// </param>
    /// <param name="toggle_keys">
    ///   The key combination that must be pressed to show/hide your overlay.
    /// </param>
    /// <param name="imgui_font_atlas">
    ///   *Can be nullptr*. Fill this parameter with your own ImGuiAtlas pointer if you don't want ImGui to generate one for you.
    /// </param>
    /// <returns></returns>
    virtual bool StartHook(std::function<bool(bool)> key_combination_callback, std::set<ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr) = 0;

    virtual bool IsStarted() = 0;

    /// <summary>
    ///   Load an RGBA ordered buffer into GPU and returns a handle to this ressource to be used by ImGui.
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
    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height) = 0;

    /// <summary>
    ///   Frees a previously image resource created with CreateImageResource.
    /// </summary>
    /// <param name="resource">
    ///   The weak_ptr to the resource. Its safe to call with an invalid weak_ptr.
    /// </param>
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource) = 0;

    /// <summary>
    ///   Get the current renderer library name.
    /// </summary>
    /// <returns></returns>
    virtual std::string GetLibraryName() const = 0;
};

}