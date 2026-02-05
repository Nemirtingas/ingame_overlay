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

#include "../RendererHookInternal.h"

#include <GL/glx.h>

namespace InGameOverlay {

class OpenGLXHook_t :
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
private:
    static OpenGLXHook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _X11Hooked;
    bool _Initialized;
    OverlayHookState _HookState;
    Display *_Display;
    //GLXContext _Context;
    std::set<std::shared_ptr<RendererTexture_t>> _ImageResources;
    std::vector<RendererTextureLoadParameter_t> _ImageResourcesToLoad;
    std::vector<RendererTextureReleaseParameter_t> _ImageResourcesToRelease;
    void* _ImGuiFontAtlas;

    // Functions
    OpenGLXHook_t();

    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(Display* display, GLXDrawable drawable);
    void _LoadResources();
    void _ReleaseResources();
    void _HandleScreenshot();

    // Hook to render functions
    decltype(::glXSwapBuffers)* _GLXSwapBuffers;

    static void _MyGLXSwapBuffers(Display* display, GLXDrawable drawable);

public:
    std::string LibraryName;

    virtual ~OpenGLXHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static OpenGLXHook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers);

    virtual std::weak_ptr<RendererTexture_t> AllocImageResource();
    virtual void LoadImageResource(RendererTextureLoadParameter_t& loadParameter);
    virtual void ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource);
};

}// namespace InGameOverlay