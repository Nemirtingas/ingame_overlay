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

#include <Windows.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

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
    std::string LibraryName;
    std::filesystem::path LibraryPath;

    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    OverlayHookState _HookState;
    HWND _LastWindow;
    std::set<std::shared_ptr<RendererTexture_t>> _ImageResources;
    std::vector<RendererTextureLoadParameter_t> _ImageResourcesToLoad;
    std::vector<RendererTextureReleaseParameter_t> _ImageResourcesToRelease;
    void* _ImGuiFontAtlas;

    // Functions
    OpenGLHook_t();

    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(HDC hDC);
    void _LoadResources();
    void _ReleaseResources();
    void _HandleScreenshot();

    // Hook to render functions
    WGLSwapBuffers_t _WGLSwapBuffers;

    static BOOL WINAPI _MyWGLSwapBuffers(HDC hDC);

public:
    virtual ~OpenGLHook_t() override;

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr) override;
    virtual void HideAppInputs(bool hide) override;
    virtual void HideOverlayInputs(bool hide) override;
    virtual bool IsStarted() override;
    static OpenGLHook_t* Inst();
    virtual const char* GetLibraryName() const override;
    virtual RendererHookType_t GetRendererHookType() const override;
    void LoadFunctions(WGLSwapBuffers_t pfnwglSwapBuffers);

    virtual std::weak_ptr<RendererTexture_t> AllocImageResource() override;
    virtual void LoadImageResource(RendererTextureLoadParameter_t& loadParameter) override;
    virtual void ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource) override;

    virtual void SetLibraryPath(std::filesystem::path const& libraryPath) override;
};

}// namespace InGameOverlay