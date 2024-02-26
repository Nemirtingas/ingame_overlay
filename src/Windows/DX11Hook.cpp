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

#include "DX11Hook.h"
#include "WindowsHook.h"

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

namespace InGameOverlay {

DX11Hook_t* DX11Hook_t::_Instance = nullptr;

template<typename T>
static inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
    }
}

static HRESULT GetDeviceAndCtxFromSwapchain(IDXGISwapChain* pSwapChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext)
{
    HRESULT ret = pSwapChain->GetDevice(IID_PPV_ARGS(ppDevice));

    if (SUCCEEDED(ret))
        (*ppDevice)->GetImmediateContext(ppContext);

    return ret;
}

bool DX11Hook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 11: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        SPDLOG_INFO("Hooked DirectX 11");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainPresent      , &DX11Hook_t::_MyIDXGISwapChainPresent),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeTarget , &DX11Hook_t::_MyIDXGISwapChainResizeBuffers),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeBuffers, &DX11Hook_t::_MyIDXGISwapChainResizeTarget)
        );
        if (_IDXGISwapChain1Present1 != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChain1Present1, &DX11Hook_t::_MyIDXGISwapChain1Present1)
            );
        }
        EndHook();
    }
    return true;
}

void DX11Hook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideAppInputs(hide);
    }
}

void DX11Hook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool DX11Hook_t::IsStarted()
{
    return _Hooked;
}

void DX11Hook_t::_ResetRenderState(OverlayHookState state)
{
    if (_Initialized)
    {
        OverlayHookReady(state);

        ImGui_ImplDX11_Shutdown();
        WindowsHook_t::Inst()->ResetRenderState(state);
        //ImGui::DestroyContext();

        _ImageResources.clear();
        SafeRelease(_MainRenderTargetView);
        SafeRelease(_DeviceContext);
        SafeRelease(_Device);

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX11Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain)
{
    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);

    if (!_Initialized)
    {
        _Device = nullptr;
        if (FAILED(GetDeviceAndCtxFromSwapchain(pSwapChain, &_Device, &_DeviceContext)))
            return;

        ID3D11Texture2D* pBackBuffer;
        pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));

        _Device->CreateRenderTargetView(pBackBuffer, NULL, &_MainRenderTargetView);

        //ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        //pContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, NULL);
        //bool bind_target = true;
        //
        //for (unsigned i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && targets[i] != nullptr; ++i)
        //{
        //    ID3D11Resource* res = NULL;
        //    targets[i]->GetResource(&res);
        //    if (res)
        //    {
        //        if (res == (ID3D11Resource*)pBackBuffer)
        //        {
        //            pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        //        }
        //
        //        res->Release();
        //    }
        //
        //    targets[i]->Release();
        //}
        
        SafeRelease(pBackBuffer);
        
        if (_MainRenderTargetView == nullptr)
            return;
        
        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));
        
        ImGui_ImplDX11_Init(_Device, _DeviceContext);
        
        WindowsHook_t::Inst()->SetInitialWindowSize(desc.OutputWindow);

        _Initialized = true;
        OverlayHookReady(OverlayHookState::Ready);
    }

    if (ImGui_ImplDX11_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(desc.OutputWindow))
    {
        ImGui::NewFrame();
    
        OverlayProc();
    
        ImGui::Render();

        if (_MainRenderTargetView)
        {
            _DeviceContext->OMSetRenderTargets(1, &_MainRenderTargetView, NULL);
        }
    
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    auto inst = DX11Hook_t::Inst();
    inst->_PrepareForOverlay(_this);
    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    auto inst = DX11Hook_t::Inst();
    inst->_ResetRenderState(OverlayHookState::Removing);
    return (_this->*inst->_IDXGISwapChainResizeTarget)(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto inst = DX11Hook_t::Inst();
    inst->_ResetRenderState(OverlayHookState::Removing);
    return (_this->*inst->_IDXGISwapChainResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    auto inst = DX11Hook_t::Inst();
    inst->_PrepareForOverlay(_this);
    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

DX11Hook_t::DX11Hook_t():
    _Initialized(false),
    _Hooked(false),
    _WindowsHooked(false),
    _ImGuiFontAtlas(nullptr),
    _DeviceContext(nullptr),
    _MainRenderTargetView(nullptr),
    _IDXGISwapChainPresent(nullptr),
    _IDXGISwapChainResizeBuffers(nullptr),
    _IDXGISwapChainResizeTarget(nullptr),
    _IDXGISwapChain1Present1(nullptr)
{
}

DX11Hook_t::~DX11Hook_t()
{
    SPDLOG_INFO("DX11 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    if (_Initialized)
    {
        SafeRelease(_MainRenderTargetView);
        SafeRelease(_DeviceContext);

        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui::DestroyContext();

        _Initialized = false;
    }

    _Instance = nullptr;
}

DX11Hook_t* DX11Hook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new DX11Hook_t;

    return _Instance;
}

const std::string& DX11Hook_t::GetLibraryName() const
{
    return LibraryName;
}

void DX11Hook_t::LoadFunctions(
    decltype(_IDXGISwapChainPresent) presentFcn,
    decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
    decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
    decltype(_IDXGISwapChain1Present1) present1Fcn)
{
    _IDXGISwapChainPresent = presentFcn;
    _IDXGISwapChainResizeBuffers = resizeBuffersFcn;
    _IDXGISwapChainResizeTarget = resizeTargetFcn;
    _IDXGISwapChain1Present1 = present1Fcn;
}

std::weak_ptr<uint64_t> DX11Hook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    ID3D11ShaderResourceView** resource = new ID3D11ShaderResourceView*(nullptr);

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = nullptr;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    _Device->CreateTexture2D(&desc, &subResource, &pTexture);

    if (pTexture != nullptr)
    {
        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        _Device->CreateShaderResourceView(pTexture, &srvDesc, resource);
        // Release Texture, the shader resource increases the reference count.
        pTexture->Release();
    }

    if (*resource == nullptr)
        return std::shared_ptr<uint64_t>();

    auto ptr = std::shared_ptr<uint64_t>((uint64_t*)resource, [](uint64_t* handle)
    {
        if(handle != nullptr)
        {
            ID3D11ShaderResourceView** resource = reinterpret_cast<ID3D11ShaderResourceView**>(handle);
            (*resource)->Release();
            delete resource;
        }
    });

    _ImageResources.emplace(ptr);

    return ptr;
}

void DX11Hook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
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