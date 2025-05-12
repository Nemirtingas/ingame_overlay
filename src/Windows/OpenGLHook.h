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

#include "../RendererHookInternal.h"

#include "../InternalIncludes.h"

namespace InGameOverlay {

class OpenGLHook_t :
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
public:
    using WGLSwapBuffers_t = BOOL(WINAPI*)(HDC);

private:
    static OpenGLHook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    OverlayHookState _HookState;
    HWND _LastWindow;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    OpenGLHook_t();

    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(HDC hDC);
    void _HandleScreenshot();
    bool _CaptureScreenshot(ScreenshotData_t& outData);

    // Hook to render functions
    WGLSwapBuffers_t _WGLSwapBuffers;

    static BOOL WINAPI _MyWGLSwapBuffers(HDC hDC);

public:
    std::string LibraryName;

    virtual ~OpenGLHook_t();

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static OpenGLHook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(WGLSwapBuffers_t pfnwglSwapBuffers);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay