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

#include "DX9_Hook.h"
#include "Windows_Hook.h"
#include "DirectX_VTables.h"

#include <imgui.h>
#include <backends/imgui_impl_dx9.h>

DX9_Hook* DX9_Hook::_inst = nullptr;

template<typename T>
inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
    }
}

bool DX9_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (Reset == nullptr || Present == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 9: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->start_hook(key_combination_callback))
            return false;

        windows_hooked = true;

        SPDLOG_INFO("Hooked DirectX 9");
        hooked = true;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)Reset, &DX9_Hook::MyReset),
            std::make_pair<void**, void*>(&(PVOID&)Present, &DX9_Hook::MyPresent)
        );
        if (PresentEx != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)PresentEx, &DX9_Hook::MyPresentEx)
                //std::make_pair<void**, void*>(&(PVOID&)EndScene, &DX9_Hook::MyEndScene)
            );
        }
        EndHook();
    }
    return true;
}

bool DX9_Hook::is_started()
{
    return hooked;
}

void DX9_Hook::resetRenderState()
{
    if (initialized)
    {
        overlay_hook_ready(false);

        ImGui_ImplDX9_Shutdown();
        Windows_Hook::Inst()->resetRenderState();
        ImGui::DestroyContext();

        SafeRelease(pDevice);
        
        last_window = nullptr;
        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX9_Hook::prepareForOverlay(IDirect3DDevice9 *pDevice)
{
    D3DDEVICE_CREATION_PARAMETERS param;
    pDevice->GetCreationParameters(&param);

    // Workaround to detect if we changed window.
    if (param.hFocusWindow != last_window || this->pDevice != pDevice)
        resetRenderState();

    if (!initialized)
    {
        pDevice->AddRef();
        this->pDevice = pDevice;

        ImGui::CreateContext();
        ImGui_ImplDX9_Init(pDevice);

        last_window = param.hFocusWindow;
        initialized = true;
        overlay_hook_ready(true);
    }

    if (ImGui_ImplDX9_NewFrame() && Windows_Hook::Inst()->prepareForOverlay(param.hFocusWindow))
    {
        ImGui::NewFrame();

        overlay_proc();

        ImGui::Render();

        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
}

HRESULT STDMETHODCALLTYPE DX9_Hook::MyReset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    auto inst = DX9_Hook::Inst();
    inst->resetRenderState();
    return (_this->*inst->Reset)(pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE DX9_Hook::MyEndScene(IDirect3DDevice9* _this)
{   
    auto inst = DX9_Hook::Inst();
    if( !inst->uses_present )
        inst->prepareForOverlay(_this);

    return (_this->*inst->EndScene)();
}

HRESULT STDMETHODCALLTYPE DX9_Hook::MyPresent(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    auto inst = DX9_Hook::Inst();
    inst->uses_present = true;
    inst->prepareForOverlay(_this);
    return (_this->*inst->Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE DX9_Hook::MyPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    auto inst = DX9_Hook::Inst();
    inst->uses_present = true;
    inst->prepareForOverlay(_this);
    return (_this->*inst->PresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

DX9_Hook::DX9_Hook():
    initialized(false),
    hooked(false),
    windows_hooked(false),
    uses_present(false),
    last_window(nullptr),
    EndScene(nullptr),
    Present(nullptr),
    PresentEx(nullptr),
    Reset(nullptr)
{
}

DX9_Hook::~DX9_Hook()
{
    SPDLOG_INFO("DX9 Hook removed");

    if (windows_hooked)
        delete Windows_Hook::Inst();

    if (initialized)
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
        ImGui::DestroyContext();
    }

    _inst = nullptr;
}

DX9_Hook* DX9_Hook::Inst()
{
    if( _inst == nullptr )
        _inst = new DX9_Hook;

    return _inst;
}

const char* DX9_Hook::get_lib_name() const
{
    return library_name.c_str();
}

void DX9_Hook::loadFunctions(decltype(Present) PresentFcn, decltype(Reset) ResetFcn, decltype(EndScene) EndSceneFcn, decltype(PresentEx) PresentExFcn)
{
    Present = PresentFcn;
    Reset = ResetFcn;
    EndScene = EndSceneFcn;

    PresentEx = PresentExFcn;
}

std::weak_ptr<uint64_t> DX9_Hook::CreateImageResource(std::shared_ptr<Image> source)
{
    IDirect3DTexture9** pTexture = new IDirect3DTexture9*(nullptr);

    pDevice->CreateTexture(
        source->width(),
        source->height(),
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
            image_pixel_t const* pixels = source->pixels();
            uint8_t* texture_bits = reinterpret_cast<uint8_t*>(rect.pBits);
            for (int32_t i = 0; i < source->height(); ++i)
            {
                for (int32_t j = 0; j < source->width(); ++j)
                {
                    // RGBA to ARGB Conversion, DX9 doesn't have a RGBA loader
                    reinterpret_cast<uint32_t*>(texture_bits)[j] = D3DCOLOR_ARGB(pixels->channels.a, pixels->channels.r, pixels->channels.g, pixels->channels.b);
                    ++pixels;
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

    image_resources.emplace(ptr);

    return ptr;
}

void DX9_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = image_resources.find(ptr);
        if (it != image_resources.end())
            image_resources.erase(it);
    }
}