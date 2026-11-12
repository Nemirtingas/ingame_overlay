/*
 * Copyright (C) 2019-2020 Nemirtingas
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

#include "../OpenGLHookCommon.h"

#include <chrono>
#include <filesystem>
#include <unordered_map>

#include <GL/glx.h>

namespace InGameOverlay {

class OpenGLXHook_t :
    public OpenGLHookCommon_t,
    public BaseHook_t
{
private:
    static OpenGLXHook_t* _Instance;

    // Variables
    std::string LibraryName;
    std::filesystem::path LibraryPath;

    bool _Hooked;
    bool _X11Hooked;
    Display *_Display;
    //GLXContext _Context;
    // The X window of each presented drawable: GLFW and others present a GLXWindow (glXCreateWindow),
    // which is not a core X window and can't be used with the window hook.
    std::unordered_map<GLXDrawable, Window> _DrawableWindows;
    std::chrono::steady_clock::time_point _NextWindowSearch;
    decltype(::glXQueryDrawable)* _GLXQueryDrawable;

    // Functions
    OpenGLXHook_t();

    Window _GetDrawableWindow(Display* display, GLXDrawable drawable);
    void _PrepareForOverlay(Display* display, GLXDrawable drawable);

    virtual bool _ImGuiBackendInit() override;
    virtual bool _ImGuiBackendNewFrame() override;
    virtual void _ImGuiBackendRenderDrawData(ImDrawData* drawData) override;
    virtual void _ImGuiBackendShutdown() override;
    virtual void _ResetWindowHook(OverlayHookState state) override;
    virtual void _ForgetWindow() override;

    // Hook to render functions
    decltype(::glXSwapBuffers)* _GLXSwapBuffers;

    static void _MyGLXSwapBuffers(Display* display, GLXDrawable drawable);

public:
    virtual ~OpenGLXHook_t() override;

    virtual bool StartHook(std::function<void()> key_combination_callback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr) override;
    virtual void HideAppInputs(bool hide) override;
    virtual void HideOverlayInputs(bool hide) override;
    virtual bool IsStarted() override;
    static OpenGLXHook_t* Inst();
    virtual const char* GetLibraryName() const override;
    virtual RendererHookType_t GetRendererHookType() const override;
    void LoadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers);

    virtual void SetLibraryPath(std::filesystem::path const& libraryPath) override;
};

}// namespace InGameOverlay