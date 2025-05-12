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

#include <d3d9.h>

namespace InGameOverlay {

class DX9Hook_t :
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
private:
    static DX9Hook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _UsesDXVK;
    HWND _LastWindow;
    uint32_t _DeviceReleasing;
    IDirect3DDevice9* _Device;
    ULONG _HookDeviceRefCount;
    OverlayHookState _HookState;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    DX9Hook_t();

    void _UpdateHookDeviceRefCount();
    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(IDirect3DDevice9* pDevice, HWND destWindow);
    void _HandleScreenshot();
    bool _CaptureScreenshot(ScreenshotData_t& outData);

    // Hook to render functions
    decltype(&IDirect3DDevice9::Release)     _IDirect3DDevice9Release;
    decltype(&IDirect3DDevice9::Reset)       _IDirect3DDevice9Reset;
    decltype(&IDirect3DDevice9::Present)     _IDirect3DDevice9Present;
    decltype(&IDirect3DDevice9Ex::PresentEx) _IDirect3DDevice9ExPresentEx;
    decltype(&IDirect3DDevice9Ex::ResetEx)   _IDirect3DDevice9ExResetEx;
    decltype(&IDirect3DSwapChain9::Present)  _IDirect3DSwapChain9SwapChainPresent;

    static ULONG   STDMETHODCALLTYPE _MyIDirect3DDevice9Release(IDirect3DDevice9* _this);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9Reset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9ExPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9ExResetEx(IDirect3DDevice9Ex* _this, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DSwapChain9SwapChainPresent(IDirect3DSwapChain9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);

public:
    std::string LibraryName;

    virtual ~DX9Hook_t();

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static DX9Hook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;

    void SetDXVK();
    void LoadFunctions(
        decltype(_IDirect3DDevice9Release) ReleaseFcn,
        decltype(_IDirect3DDevice9Present) PresentFcn,
        decltype(_IDirect3DDevice9Reset) ResetFcn,
        decltype(_IDirect3DDevice9ExPresentEx) PresentExFcn,
        decltype(_IDirect3DDevice9ExResetEx) ResetExFcn,
        decltype(_IDirect3DSwapChain9SwapChainPresent) SwapChainPresentFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay