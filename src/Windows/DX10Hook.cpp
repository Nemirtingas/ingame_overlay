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

#include "DX10Hook.h"
#include "WindowsHook.h"

#include <imgui.h>
#include <backends/imgui_impl_dx10.h>

namespace InGameOverlay {

DX10Hook_t* DX10Hook_t::_Instance = nullptr;

template<typename T>
static inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
    }
}

bool DX10Hook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_ID3D10DeviceRelease == nullptr || _IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 11: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        SPDLOG_INFO("Hooked DirectX 10");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)_ID3D10DeviceRelease        , &DX10Hook_t::_MyID3D10DeviceRelease),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainPresent      , &DX10Hook_t::_MyIDXGISwapChainPresent),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeTarget , &DX10Hook_t::_MyIDXGISwapChainResizeTarget),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeBuffers, &DX10Hook_t::_MyIDXGISwapChainResizeBuffers)
        );
        if (_IDXGISwapChain1Present1 != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChain1Present1, &DX10Hook_t::_MyIDXGISwapChain1Present1)
            );
        }
        EndHook();
    }
    return true;
}

void DX10Hook_t::HideAppInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideAppInputs(hide);
}

void DX10Hook_t::HideOverlayInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
}

bool DX10Hook_t::IsStarted()
{
    return _Hooked;
}

void DX10Hook_t::_UpdateHookDeviceRefCount()
{
    switch (_HookState)
    {
        // 0 ref from ImGui
        case OverlayHookState::Removing: _HookDeviceRefCount = 2; break;
        // 1 ref from us, 10 refs from ImGui (device, vertex shader, input layout, vertex constant buffer, pixel shader, blend state, rasterizer state, depth stencil state, texture view, texture sample)
        //case OverlayHookState::Reset: _HookDeviceRefCount = 13 + _ImageResources.size(); break;
        // 1 ref from us, 12 refs from ImGui (device, vertex shader, input layout, vertex constant buffer, pixel shader, blend state, rasterizer state, depth stencil state, texture view, texture sample, vertex buffer, index buffer)
        case OverlayHookState::Ready: _HookDeviceRefCount = 15 + _ImageResources.size();
    }
}

bool DX10Hook_t::_CreateRenderTargets(IDXGISwapChain* pSwapChain)
{
    ID3D10Texture2D* pBackBuffer;
    ID3D10RenderTargetView* pRenderTargetView;
    bool result = true;

    if (!SUCCEEDED(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) || pBackBuffer == nullptr)
        return false;

    if (!SUCCEEDED(_Device->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) || pRenderTargetView == nullptr)
        result = false;

    pBackBuffer->Release();
    _RenderTargetView = pRenderTargetView;

    return result;
}

void DX10Hook_t::_DestroyRenderTargets()
{
    SafeRelease(_RenderTargetView);
}

void DX10Hook_t::_ResetRenderState(OverlayHookState state)
{
    if (_HookState == state)
        return;

    OverlayHookReady(state);

    _HookState = state;
    _UpdateHookDeviceRefCount();
    switch (state)
    {
        case OverlayHookState::Removing:
            _DeviceReleasing = true;
            ImGui_ImplDX10_Shutdown();
            WindowsHook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();
            _DestroyRenderTargets();
            SafeRelease(_Device);

            _DeviceReleasing = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX10Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain)
{
    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);

    if (_HookState == OverlayHookState::Removing)
    {
        if (FAILED(pSwapChain->GetDevice(IID_PPV_ARGS(&_Device))))
            return;

        if (!_CreateRenderTargets(pSwapChain))
        {
            SafeRelease(_Device);
            return;
        }

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX10_Init(_Device);

        WindowsHook_t::Inst()->SetInitialWindowSize(desc.OutputWindow);

        if (!ImGui_ImplDX10_CreateDeviceObjects())
        {
            ImGui_ImplDX10_Shutdown();
            _DestroyRenderTargets();
            SafeRelease(_Device);
            return;
        }

        _HookState = OverlayHookState::Ready;
        _UpdateHookDeviceRefCount();

        OverlayHookReady(_HookState);
    }

    if (ImGui_ImplDX10_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(desc.OutputWindow))
    {
        ImGui::NewFrame();

        OverlayProc();

        ImGui::Render();

        _Device->OMSetRenderTargets(1, &_RenderTargetView, nullptr);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());
    }
}

ULONG STDMETHODCALLTYPE DX10Hook_t::_MyID3D10DeviceRelease(ID3D10Device* _this)
{
    auto inst = DX10Hook_t::Inst();
    auto result = (_this->*inst->_ID3D10DeviceRelease)();

    SPDLOG_INFO("ID3D10Device::Release: RefCount = {}, Our removal threshold = {}", result, inst->_HookDeviceRefCount);

    if (!inst->_DeviceReleasing && _this == inst->_Device && result < inst->_HookDeviceRefCount)
        inst->_ResetRenderState(OverlayHookState::Removing);

    return result;
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    SPDLOG_INFO("IDXGISwapChain::Present");
    auto inst = DX10Hook_t::Inst();
    inst->_PrepareForOverlay(_this);
    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    SPDLOG_INFO("IDXGISwapChain::ResizeBuffers");
    auto inst = DX10Hook_t::Inst();

    inst->OverlayHookReady(OverlayHookState::Reset);
    inst->_DestroyRenderTargets();
    auto r = (_this->*inst->_IDXGISwapChainResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    inst->_CreateRenderTargets(_this);

    return r;
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    SPDLOG_INFO("IDXGISwapChain::ResizeTarget");
    auto inst = DX10Hook_t::Inst();

    inst->OverlayHookReady(OverlayHookState::Reset);
    inst->_DestroyRenderTargets();
    auto r = (_this->*inst->_IDXGISwapChainResizeTarget)(pNewTargetParameters);
    inst->_CreateRenderTargets(_this);

    return r;
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    SPDLOG_INFO("IDXGISwapChain1::Present1");
    auto inst = DX10Hook_t::Inst();
    inst->_PrepareForOverlay(_this);
    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

DX10Hook_t::DX10Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _DeviceReleasing(false),
    _Device(nullptr),
    _HookDeviceRefCount(0),
    _HookState(OverlayHookState::Removing),
    _RenderTargetView(nullptr),
    _ImGuiFontAtlas(nullptr),
    _ID3D10DeviceRelease(nullptr),
    _IDXGISwapChainPresent(nullptr),
    _IDXGISwapChainResizeBuffers(nullptr),
    _IDXGISwapChainResizeTarget(nullptr),
    _IDXGISwapChain1Present1(nullptr)
{
}

DX10Hook_t::~DX10Hook_t()
{
    SPDLOG_INFO("DX10 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    _Instance = nullptr;
}

DX10Hook_t* DX10Hook_t::Inst()
{
    if (_Instance == nullptr)   
        _Instance = new DX10Hook_t;

    return _Instance;
}

const std::string& DX10Hook_t::GetLibraryName() const
{
    return LibraryName;
}

void DX10Hook_t::LoadFunctions(
    decltype(_ID3D10DeviceRelease) releaseFcn,
    decltype(_IDXGISwapChainPresent) presentFcn,
    decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
    decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
    decltype(_IDXGISwapChain1Present1) present1Fcn)
{
    _ID3D10DeviceRelease = releaseFcn;

    _IDXGISwapChainPresent = presentFcn;
    _IDXGISwapChainResizeBuffers = resizeBuffersFcn;
    _IDXGISwapChainResizeTarget = resizeTargetFcn;

    _IDXGISwapChain1Present1 = present1Fcn;
}

std::weak_ptr<uint64_t> DX10Hook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    ID3D10ShaderResourceView** resource = new ID3D10ShaderResourceView*(nullptr);

    // Create texture
    D3D10_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D10Texture2D* pTexture = nullptr;
    D3D10_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    _Device->CreateTexture2D(&desc, &subResource, &pTexture);

    if (pTexture != nullptr)
    {
        // Create texture view
        D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        _Device->CreateShaderResourceView(pTexture, &srvDesc, resource);
        // Release Texure, the shader resource increases the reference count.
        pTexture->Release();
    }

    if (*resource == nullptr)
        return std::shared_ptr<uint64_t>();

    auto ptr = std::shared_ptr<uint64_t>((uint64_t*)resource, [](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            ID3D10ShaderResourceView** resource = reinterpret_cast<ID3D10ShaderResourceView**>(handle);
            (*resource)->Release();
            delete resource;
        }
    });

    _ImageResources.emplace(ptr);

    _UpdateHookDeviceRefCount();

    return ptr;
}

void DX10Hook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
        {
            _ImageResources.erase(it);
            _UpdateHookDeviceRefCount();
        }
    }
}

}// namespace InGameOverlay