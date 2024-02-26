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

#include "DX9Hook.h"
#include "WindowsHook.h"
#include "DirectXVTables.h"

#include <imgui.h>
#include <backends/imgui_impl_dx9.h>

namespace InGameOverlay {

DX9Hook_t* DX9Hook_t::_Instance = nullptr;

template<typename T>
static inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
    }
}

bool DX9Hook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_IDirect3DDevice9Reset == nullptr || _IDirect3DDevice9Present == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 9: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        SPDLOG_INFO("Hooked DirectX 9");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)_IDirect3DDevice9Reset, &DX9Hook_t::_MyIDirect3DDevice9Reset),
            std::make_pair<void**, void*>(&(PVOID&)_IDirect3DDevice9Present, &DX9Hook_t::_MyIDirect3DDevice9Present)
        );
        if (_IDirect3DDevice9ExPresentEx != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDirect3DDevice9ExPresentEx, &DX9Hook_t::_MyIDirect3DDevice9ExPresentEx)
            );
        }
        if (_IDirect3DSwapChain9SwapChainPresent != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDirect3DSwapChain9SwapChainPresent, &DX9Hook_t::_MyIDirect3DSwapChain9SwapChainPresent)
            );
        }
        EndHook();
    }
    return true;
}

void DX9Hook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideAppInputs(hide);
    }
}

void DX9Hook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool DX9Hook_t::IsStarted()
{
    return _Hooked;
}

void DX9Hook_t::_ResetRenderState(OverlayHookState state)
{
    if (_Initialized)
    {
        OverlayHookReady(state);

        ImGui_ImplDX9_Shutdown();
        WindowsHook_t::Inst()->ResetRenderState(state);
        //ImGui::DestroyContext();

        _ImageResources.clear();
        SafeRelease(_pDevice);
        
        _LastWindow = nullptr;
        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX9Hook_t::_PrepareForOverlay(IDirect3DDevice9 *pDevice, HWND destWindow)
{
    if (!destWindow)
    {
        IDirect3DSwapChain9 *pSwapChain = nullptr;
        if (pDevice->GetSwapChain(0, &pSwapChain) == D3D_OK)
        {
            D3DPRESENT_PARAMETERS params;
            if (pSwapChain->GetPresentParameters(&params) == D3D_OK)
            {
                destWindow = params.hDeviceWindow;
            }

            pSwapChain->Release();
        }
    }

    //Is this necessary anymore?
    if (!destWindow)
    {
        D3DDEVICE_CREATION_PARAMETERS param;
        pDevice->GetCreationParameters(&param);
        destWindow = param.hFocusWindow;
    }

    // Workaround to detect if we changed window.
    if (destWindow != _LastWindow || _pDevice != pDevice)
        _ResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        pDevice->AddRef();
        _pDevice = pDevice;

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX9_Init(pDevice);

        _LastWindow = destWindow;

        WindowsHook_t::Inst()->SetInitialWindowSize(destWindow);

        _Initialized = true;
        OverlayHookReady(OverlayHookState::Ready);
    }

    if (ImGui_ImplDX9_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(destWindow))
    {
        ImGui::NewFrame();

        OverlayProc();

        ImGui::Render();

        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9Reset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    auto inst = DX9Hook_t::Inst();
    inst->_ResetRenderState(OverlayHookState::Removing);
    return (_this->*inst->_IDirect3DDevice9Reset)(pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    auto inst = DX9Hook_t::Inst();
    inst->_PrepareForOverlay(_this, hDestWindowOverride);
    return (_this->*inst->_IDirect3DDevice9Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9ExPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    auto inst = DX9Hook_t::Inst();
    inst->_PrepareForOverlay(_this, hDestWindowOverride);
    return (_this->*inst->_IDirect3DDevice9ExPresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DSwapChain9SwapChainPresent(IDirect3DSwapChain9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    IDirect3DDevice9* pDevice;
    auto inst = DX9Hook_t::Inst();

    if (SUCCEEDED(_this->GetDevice(&pDevice)))
    {
        HWND destWindow = hDestWindowOverride;
        if (!destWindow)
        {
            D3DPRESENT_PARAMETERS param;
            if (_this->GetPresentParameters(&param) == D3D_OK)
            {
                destWindow = param.hDeviceWindow;
            }
        }

        inst->_PrepareForOverlay(pDevice, destWindow);
        pDevice->Release();
    }
    return (_this->*inst->_IDirect3DSwapChain9SwapChainPresent)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

DX9Hook_t::DX9Hook_t():
    _Initialized(false),
    _Hooked(false),
    _WindowsHooked(false),
    _LastWindow(nullptr),
    _ImGuiFontAtlas(nullptr),
    _IDirect3DDevice9Present(nullptr),
    _IDirect3DDevice9ExPresentEx(nullptr),
    _IDirect3DDevice9Reset(nullptr)
{
}

DX9Hook_t::~DX9Hook_t()
{
    SPDLOG_INFO("DX9 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    if (_Initialized)
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
        ImGui::DestroyContext();
    }

    _Instance = nullptr;
}

DX9Hook_t* DX9Hook_t::Inst()
{
    if( _Instance == nullptr )
        _Instance = new DX9Hook_t;

    return _Instance;
}

const std::string& DX9Hook_t::GetLibraryName() const
{
    return LibraryName;
}

void DX9Hook_t::LoadFunctions(
    decltype(_IDirect3DDevice9Present) PresentFcn,
    decltype(_IDirect3DDevice9Reset) ResetFcn,
    decltype(_IDirect3DDevice9ExPresentEx) PresentExFcn,
    decltype(_IDirect3DSwapChain9SwapChainPresent) SwapChainPresentFcn)
{
    _IDirect3DDevice9Present = PresentFcn;
    _IDirect3DDevice9Reset = ResetFcn;

    _IDirect3DDevice9ExPresentEx = PresentExFcn;

    _IDirect3DSwapChain9SwapChainPresent = SwapChainPresentFcn;
}

std::weak_ptr<uint64_t> DX9Hook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    IDirect3DTexture9** pTexture = new IDirect3DTexture9*(nullptr);

    _pDevice->CreateTexture(
        width,
        height,
        1,
        D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        pTexture,
        nullptr
    );

    if (*pTexture != nullptr)
    {
        D3DLOCKED_RECT rect;
        if (SUCCEEDED((*pTexture)->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD)))
        {
            const uint32_t* pixels = reinterpret_cast<const uint32_t*>(image_data);
            uint8_t* texture_bits = reinterpret_cast<uint8_t*>(rect.pBits);
            for (uint32_t i = 0; i < height; ++i)
            {
                for (uint32_t j = 0; j < width; ++j)
                {
                    // RGBA to ARGB Conversion, DX9 doesn't have a RGBA loader
                    uint32_t color = *pixels++;
                    reinterpret_cast<uint32_t*>(texture_bits)[j] = ((color & 0xff) << 16) | (color & 0xff00) | ((color & 0xff0000) >> 16) | (color & 0xff000000);
                }
                texture_bits += rect.Pitch;
            }

            if (FAILED((*pTexture)->UnlockRect(0)))
            {
                (*pTexture)->Release();
                delete pTexture;
                pTexture = nullptr;
            }
        }
        else
        {
            (*pTexture)->Release();
            delete pTexture;
            pTexture = nullptr;
        }
    }

    if (pTexture == nullptr)
        return std::shared_ptr<uint64_t>();

    auto ptr = std::shared_ptr<uint64_t>((uint64_t*)pTexture, [](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            IDirect3DTexture9** resource = reinterpret_cast<IDirect3DTexture9**>(handle);
            (*resource)->Release();
            delete resource;
        }
    });

    _ImageResources.emplace(ptr);

    return ptr;
}

void DX9Hook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}

}// namespace InGameOverlay