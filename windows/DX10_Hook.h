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

#include "../Renderer_Hook.h"

#include <d3d10.h>

class DX10_Hook : public Renderer_Hook
{
public:
    static constexpr const char *DLL_NAME = "d3d10.dll";

private:
    static DX10_Hook* _inst;

    // Variables
    bool hooked;
    bool windows_hooked;
    bool initialized;
    ID3D10Device* pDevice;
    ID3D10RenderTargetView* mainRenderTargetView;
    std::set<std::shared_ptr<uint64_t>> image_resources;

    // Functions
    DX10_Hook();

    void resetRenderState();
    void prepareForOverlay(IDXGISwapChain *pSwapChain);

    // Hook to render functions
    static HRESULT STDMETHODCALLTYPE MyPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT STDMETHODCALLTYPE MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

    decltype(&IDXGISwapChain::Present)       Present;
    decltype(&IDXGISwapChain::ResizeBuffers) ResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget)  ResizeTarget;

public:
    virtual ~DX10_Hook();

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback);
    virtual bool is_started();
    static DX10_Hook* Inst();
    virtual const char* get_lib_name() const;

    void loadFunctions(decltype(Present) PresentFcn, decltype(ResizeBuffers) ResizeBuffersFcn, decltype(ResizeTarget) ResizeTargetFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(std::shared_ptr<Image> source);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
