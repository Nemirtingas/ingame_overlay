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

#include <d3d9.h>

class DX9_Hook : public Renderer_Hook
{
public:
    static constexpr const char *DLL_NAME = "d3d9.dll";

private:
    static DX9_Hook* _inst;

    // Variables
    bool hooked;
    bool windows_hooked;
    bool initialized;
    bool uses_present;
    HWND last_window;
    IDirect3DDevice9* pDevice;
    std::set<std::shared_ptr<uint64_t>> image_resources;

    // Functions
    DX9_Hook();

    void resetRenderState();
    void prepareForOverlay(IDirect3DDevice9* pDevice);

    // Hook to render functions
    decltype(&IDirect3DDevice9::Reset)       Reset;
    decltype(&IDirect3DDevice9::EndScene)    EndScene;
    decltype(&IDirect3DDevice9::Present)     Present;
    decltype(&IDirect3DDevice9Ex::PresentEx) PresentEx;

    static HRESULT STDMETHODCALLTYPE MyReset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static HRESULT STDMETHODCALLTYPE MyEndScene(IDirect3DDevice9 *_this);
    static HRESULT STDMETHODCALLTYPE MyPresent(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
    static HRESULT STDMETHODCALLTYPE MyPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);

public:
    virtual ~DX9_Hook();

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback);
    virtual bool is_started();
    static DX9_Hook* Inst();
    virtual const char* get_lib_name() const;

    void loadFunctions(decltype(Present) PresentFcn, decltype(Reset) ResetFcn, decltype(EndScene) EndSceneFcn, decltype(PresentEx) PresentExFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(std::shared_ptr<Image> source);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
