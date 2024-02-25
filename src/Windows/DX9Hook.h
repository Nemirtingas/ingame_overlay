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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <d3d9.h>

namespace InGameOverlay {

class DX9Hook_t :
    public RendererHook_t,
    public BaseHook_t
{
public:
    static constexpr const char *DLL_NAME = "d3d9.dll";

private:
    static DX9Hook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    HWND _LastWindow;
    IDirect3DDevice9* _pDevice;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    DX9Hook_t();

    void _ResetRenderState();
    void _PrepareForOverlay(IDirect3DDevice9* pDevice, HWND destWindow);

    // Hook to render functions
    decltype(&IDirect3DDevice9::Reset)       _IDirect3DDevice9Reset;
    decltype(&IDirect3DDevice9::Present)     _IDirect3DDevice9Present;
    decltype(&IDirect3DDevice9Ex::PresentEx) _IDirect3DDevice9ExPresentEx;
    decltype(&IDirect3DSwapChain9::Present)  _IDirect3DSwapChain9SwapChainPresent;

    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9Reset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DDevice9ExPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
    static HRESULT STDMETHODCALLTYPE _MyIDirect3DSwapChain9SwapChainPresent(IDirect3DSwapChain9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);

public:
    std::string LibraryName;

    virtual ~DX9Hook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static DX9Hook_t* Inst();
    virtual const std::string& GetLibraryName() const;

    void LoadFunctions(decltype(_IDirect3DDevice9Present) PresentFcn, decltype(_IDirect3DDevice9Reset) ResetFcn, decltype(_IDirect3DDevice9ExPresentEx) PresentExFcn, decltype(&IDirect3DSwapChain9::Present) SwapChainPresentFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay