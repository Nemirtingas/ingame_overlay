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

#include <d3d10.h>
#include <dxgi1_2.h>

namespace InGameOverlay {

class DX10Hook_t :
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
private:
    static DX10Hook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _UsesDXVK;
    uint32_t _DeviceReleasing;
    ID3D10Device* _Device;
    ULONG _HookDeviceRefCount;
    OverlayHookState _HookState;
    ID3D10RenderTargetView* _RenderTargetView;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    DX10Hook_t();

    void _UpdateHookDeviceRefCount();
    bool _CreateRenderTargets(IDXGISwapChain *pSwapChain);
    void _DestroyRenderTargets();
    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(IDXGISwapChain *pSwapChain, UINT flags);

    // Hook to render functions
    decltype(&ID3D10Device::Release)         _ID3D10DeviceRelease;
    decltype(&IDXGISwapChain::Present)       _IDXGISwapChainPresent;
    decltype(&IDXGISwapChain::ResizeBuffers) _IDXGISwapChainResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget)  _IDXGISwapChainResizeTarget;
    decltype(&IDXGISwapChain1::Present1)     _IDXGISwapChain1Present1;

    static ULONG   STDMETHODCALLTYPE _MyID3D10DeviceRelease(ID3D10Device* _this);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);

public:
    std::string LibraryName;

    virtual ~DX10Hook_t();

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static DX10Hook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;

    void SetDXVK();
    void LoadFunctions(
        decltype(_ID3D10DeviceRelease) releaseFcn,
        decltype(_IDXGISwapChainPresent) presentFcn,
        decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
        decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
        decltype(_IDXGISwapChain1Present1) present1Fcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay