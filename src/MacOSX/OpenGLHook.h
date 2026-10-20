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

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

#include "../OpenGLHookCommon.h"

#include <OpenGL/OpenGL.h>

#include <objc/runtime.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

//struct CGLDrawable_t;
//extern "C" CGLError CGLFlushDrawable(CGLDrawable_t*);

struct ImDrawData;

namespace InGameOverlay {

class OpenGLHook_t :
    public OpenGLHookCommon_t,
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
    std::string LibraryName;
    std::filesystem::path LibraryPath;

    bool _Hooked;
    bool _NSViewHooked;
    OpenGLDriver_t _OpenGLDriver;

    // Functions
    OpenGLHook_t();

    void _PrepareForOverlay();

    virtual bool _ImGuiBackendInit() override;
    virtual bool _ImGuiBackendNewFrame() override;
    virtual void _ImGuiBackendRenderDrawData(ImDrawData* drawData) override;
    virtual void _ImGuiBackendShutdown() override;
    virtual void _ResetWindowHook(OverlayHookState state) override;
    virtual void _ForgetWindow() override;

    // Hook to render functions
    Method _NSOpenGLContextFlushBufferMethod;
    CGLError (*_NSOpenGLContextflushBuffer)(id self);
    decltype(::CGLFlushDrawable)* _CGLFlushDrawable;

    static CGLError _MyNSOpenGLContextFlushBuffer(id self);
    static CGLError _MyCGLFlushDrawable(CGLContextObj glDrawable);

public:
    virtual ~OpenGLHook_t() override;

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr) override;
    virtual void HideAppInputs(bool hide) override;
    virtual void HideOverlayInputs(bool hide) override;
    virtual bool IsStarted() override;
    static OpenGLHook_t* Inst();
    virtual const char* GetLibraryName() const override;
    virtual RendererHookType_t GetRendererHookType() const override;
    void LoadFunctions(Method openGLFlushBufferMethod, decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable);

    virtual void SetLibraryPath(std::filesystem::path const& libraryPath) override;
};

}// namespace InGameOverlay