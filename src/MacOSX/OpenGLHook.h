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

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <OpenGL/OpenGL.h>

#include <objc/runtime.h>

//struct CGLDrawable_t;
//extern "C" CGLError CGLFlushDrawable(CGLDrawable_t*);

struct ImDrawData;

namespace InGameOverlay {

class OpenGLHook_t :
    public RendererHook_t,
    public BaseHook_t
{
private:
    struct OpenGLDriver_t
    {
        bool (*ImGuiInit)();
        bool (*ImGuiNewFrame)();
        void (*ImGuiRenderDrawData)(ImDrawData*);
        void (*ImGuiShutdown)();
    };

    static OpenGLHook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _NSViewHooked;
    bool _Initialized;
    OpenGLDriver_t _OpenGLDriver;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    OpenGLHook_t();

    void _ResetRenderState();
    void _PrepareForOverlay();

    // Hook to render functions
    Method _NSOpenGLContextFlushBufferMethod;
    CGLError (*_NSOpenGLContextflushBuffer)(id self);
    decltype(::CGLFlushDrawable)* _CGLFlushDrawable;

    static CGLError _MyNSOpenGLContextFlushBuffer(id self);
    static CGLError _MyCGLFlushDrawable(CGLContextObj glDrawable);

public:
    std::string LibraryName;

    virtual ~OpenGLHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static OpenGLHook_t* Inst();
    virtual const std::string& GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(Method openGLFlushBufferMethod, decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay