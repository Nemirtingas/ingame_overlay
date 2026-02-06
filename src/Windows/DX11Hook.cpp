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
#include <imgui_internal.h>
#include <backends/imgui_impl_dx11.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX11Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
} } while(0)

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX11Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

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

static InGameOverlay::ScreenshotDataFormat_t RendererFormatToScreenshotFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM     : return InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;
        case DXGI_FORMAT_B8G8R8A8_UNORM     : return InGameOverlay::ScreenshotDataFormat_t::B8G8R8A8;
        case DXGI_FORMAT_B8G8R8X8_UNORM     : return InGameOverlay::ScreenshotDataFormat_t::B8G8R8X8;
        case DXGI_FORMAT_R10G10B10A2_UNORM  : return InGameOverlay::ScreenshotDataFormat_t::R10G10B10A2;
        case DXGI_FORMAT_B5G6R5_UNORM       : return InGameOverlay::ScreenshotDataFormat_t::B5G6R5;
        case DXGI_FORMAT_B5G5R5A1_UNORM     : return InGameOverlay::ScreenshotDataFormat_t::B5G5R5A1;
        case DXGI_FORMAT_R16G16B16A16_FLOAT : return InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_UNORM : return InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return InGameOverlay::ScreenshotDataFormat_t::B8G8R8A8;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return InGameOverlay::ScreenshotDataFormat_t::B8G8R8X8;
        default:                              return InGameOverlay::ScreenshotDataFormat_t::Unknown;
    }
}

static inline HRESULT GetDeviceAndCtxFromSwapchain(IDXGISwapChain* pSwapChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext)
{
    HRESULT ret = pSwapChain->GetDevice(IID_PPV_ARGS(ppDevice));

    if (SUCCEEDED(ret))
        (*ppDevice)->GetImmediateContext(ppContext);

    return ret;
}

bool DX11Hook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_ID3D11DeviceRelease == nullptr || _IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook DirectX 11: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(ID3D11DeviceRelease);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainPresent);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeTarget);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeBuffers);

        if (_IDXGISwapChain1Present1 != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChain1Present1);

        EndHook();

        INGAMEOVERLAY_INFO("Hooked DirectX 11");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void DX11Hook_t::HideAppInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideAppInputs(hide);
}

void DX11Hook_t::HideOverlayInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
}

bool DX11Hook_t::IsStarted()
{
    return _Hooked;
}

void DX11Hook_t::_UpdateHookDeviceRefCount()
{
    switch (_HookState)
    {
        case OverlayHookState::Removing: _HookDeviceRefCount =
            + 1 // ?

            // _PrepareForOverlay
            + 1 // ID3D11Device GetDeviceAndCtxFromSwapchain

            // ImGui_ImplDX11_Init
            + 1 // ImGui ID3D11Device AddRef
            ;
            break;

        case OverlayHookState::Ready: _HookDeviceRefCount =
            + 1 // ?

            // ImGui_ImplDX11_CreateDeviceObjects
            + 1 // ID3D11BlendState (singleton per device)
            + 1 // ID3D11RasterizerState (singleton per device)
            + 1 // ID3D11DepthStencilState (singleton per device)
            + 1 // ID3D11SamplerState (singleton per device)

            // _PrepareForOverlay
            + 1 // ID3D11Device GetDeviceAndCtxFromSwapchain

            // _CreateRenderTargets
            + 1 // _RenderTargetView

            // ImGui_ImplDX11_Init
            + 1 // ImGui ID3D11Device AddRef

            // ImGui_ImplDX11_CreateDeviceObjects
            + 1 // ImGui Vertex Shader
            + 1 // ImGui Input Layout
            + 1 // ImGui Constant Buffer
            + 1 // ImGui Pixel Shader

            // ImGui_ImplDX11_RenderDrawData
            + 1 // ImGui Vertex Buffer
            + 1 // ImGui Index Buffer
            + 1 // ImGui Font Texture
            + 1 // ImGui Font Shader View

            + _ImageResourcesToRelease.size()
            + std::count_if(_ImageResources.begin(), _ImageResources.end(), [](std::shared_ptr<RendererTexture_t> tex) { return tex->LoadStatus == RendererTextureStatus_e::Loaded; })
            ;
    }
}

bool DX11Hook_t::_CreateRenderTargets(IDXGISwapChain* pSwapChain)
{
    ID3D11Texture2D* pBackBuffer;
    ID3D11RenderTargetView* pRenderTargetView;
    bool result = true;

    // Happens when the functions have been hooked, but the DX hook is not setup yet.
    if (_Device == nullptr)
        return false;

    if (!SUCCEEDED(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) || pBackBuffer == nullptr)
        return false;

    if (!SUCCEEDED(_Device->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) || pRenderTargetView == nullptr)
        result = false;

    // This code works on some apps and doesn't on others,
    // while always getting the first buffer seems to be more reliable, comment it for now.
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

    pBackBuffer->Release();
    _RenderTargetView = pRenderTargetView;

    return result;
}

void DX11Hook_t::_DestroyRenderTargets()
{
    SafeRelease(_RenderTargetView);
}

void DX11Hook_t::_ResetRenderState(OverlayHookState state)
{
    if (_HookState == state)
        return;

    if (state == OverlayHookState::Removing)
        ++_DeviceReleasing;

    OverlayHookReady(state);

    _HookState = state;
    _UpdateHookDeviceRefCount();
    switch (state)
    {
        case OverlayHookState::Removing:
            ImGui_ImplDX11_Shutdown();
            WindowsHook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();
            _ImageResourcesToLoad.clear();
            _ImageResourcesToRelease.clear();
            _DestroyRenderTargets();
            SafeRelease(_DeviceContext);
            SafeRelease(_Device);
            break;

        case OverlayHookState::Reset:
            _DestroyRenderTargets();
    }

    if (state == OverlayHookState::Removing)
        --_DeviceReleasing;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX11Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST)
        return;

    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);

    if (_HookState == OverlayHookState::Removing)
    {
        if (FAILED(GetDeviceAndCtxFromSwapchain(pSwapChain, &_Device, &_DeviceContext)))
            return;

        if (!_CreateRenderTargets(pSwapChain))
        {
            SafeRelease(_DeviceContext);
            SafeRelease(_Device);
            return;
        }

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX11_Init(_Device, _DeviceContext);

        WindowsHook_t::Inst()->SetInitialWindowSize(desc.OutputWindow);

        if (!ImGui_ImplDX11_CreateDeviceObjects())
        {
            ImGui_ImplDX11_Shutdown();
            _DestroyRenderTargets();
            SafeRelease(_DeviceContext);
            SafeRelease(_Device);
            return;
        }

        _ResetRenderState(OverlayHookState::Ready);
    }

    if (ImGui_ImplDX11_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(desc.OutputWindow))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot(pSwapChain);

        const bool has_textures = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
        ImFontAtlasUpdateNewFrame(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas), ImGui::GetFrameCount(), has_textures);

        ++_CurrentFrame;
        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();
        _ReleaseResources();

        ImGui::Render();

        _DeviceContext->OMSetRenderTargets(1, &_RenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot(pSwapChain);
    }
}

void DX11Hook_t::_LoadResources()
{
    if (_ImageResourcesToLoad.empty())
        return;

    struct ValidTexture_t
    {
        std::shared_ptr<RendererTexture_t> Resource;
        const void* Data;
        uint32_t Width;
        uint32_t Height;
    };

    std::vector<ValidTexture_t> validResources;

    const auto loadParameterCount = _ImageResourcesToLoad.size() > _BatchSize ? _BatchSize : _ImageResourcesToLoad.size();

    for (size_t i = 0; i < loadParameterCount; ++i)
    {
        auto& param = _ImageResourcesToLoad[i];

        auto r = param.Resource.lock();
        if (!r)
            continue;

        validResources.push_back({
            r,
            param.Data,
            param.Width,
            param.Height
        });
    }

    if (validResources.empty())
        return;

    for (auto& tex : validResources)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = tex.Width;
        desc.Height = tex.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = tex.Data;
        sub.SysMemPitch = tex.Width * 4;

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = _Device->CreateTexture2D(&desc, &sub, &texture);
        IM_ASSERT(SUCCEEDED(hr));

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = _Device->CreateShaderResourceView(texture, &srvDesc, &srv);
        IM_ASSERT(SUCCEEDED(hr));
        // Release Texure, the shader resource increases the reference count.
        texture->Release();

        tex.Resource->ImGuiTextureId = reinterpret_cast<uint64_t>(srv);
        tex.Resource->LoadStatus = RendererTextureStatus_e::Loaded;
    }

    _ImageResourcesToLoad.erase(
        _ImageResourcesToLoad.begin(),
        _ImageResourcesToLoad.begin() + loadParameterCount);

    _UpdateHookDeviceRefCount();
}

void DX11Hook_t::_ReleaseResources()
{
    if (_ImageResourcesToRelease.empty())
        return;

    _ImageResourcesToRelease.clear();
    _UpdateHookDeviceRefCount();
}


void DX11Hook_t::_HandleScreenshot(IDXGISwapChain* pSwapChain)
{
    bool result = false;
    ID3D11Texture2D* backBuffer = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;

    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mappedResource;

    HRESULT hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
        goto cleanup;

    backBuffer->GetDesc(&desc);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    hr = _Device->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr) || stagingTexture == nullptr)
        goto cleanup;

    _DeviceContext->CopyResource(stagingTexture, backBuffer);

    hr = _DeviceContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr))
        goto cleanup;

    ScreenshotCallbackParameter_t screenshot;
    screenshot.Width = desc.Width;
    screenshot.Height = desc.Height;
    screenshot.Pitch = mappedResource.RowPitch;
    screenshot.Data = reinterpret_cast<void*>(mappedResource.pData);
    screenshot.Format = RendererFormatToScreenshotFormat(desc.Format);

    _SendScreenshot(&screenshot);

    _DeviceContext->Unmap(stagingTexture, 0);

    result = true;

cleanup:

    SafeRelease(stagingTexture);
    SafeRelease(backBuffer);

    if (!result)
        _SendScreenshot(nullptr);
}

ULONG STDMETHODCALLTYPE DX11Hook_t::_MyID3D11DeviceRelease(ID3D11Device* _this)
{
    auto inst = DX11Hook_t::Inst();
    auto result = (_this->*inst->_ID3D11DeviceRelease)();

    if (_this == inst->_Device)
    {
        INGAMEOVERLAY_INFO("ID3D11Device::Release: RefCount = {}, Our removal threshold = {}", result, inst->_HookDeviceRefCount);

        if (inst->_DeviceReleasing == 0 && result <= inst->_HookDeviceRefCount)
            inst->_ResetRenderState(OverlayHookState::Removing);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::Present");
    auto inst = DX11Hook_t::Inst();
    inst->_PrepareForOverlay(_this, Flags);
    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeBuffers");
    auto inst = DX11Hook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_Device != nullptr && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = true;
        inst->_ResetRenderState(OverlayHookState::Reset);
    }
    auto r = (_this->*inst->_IDXGISwapChainResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (createRenderTargets)
    {
        inst->_ResetRenderState(inst->_CreateRenderTargets(_this)
            ? OverlayHookState::Ready
            : OverlayHookState::Removing);
    }

    return r;
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeTarget");
    auto inst = DX11Hook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_Device != nullptr && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = true;
        inst->_ResetRenderState(OverlayHookState::Reset);
    }
    auto r = (_this->*inst->_IDXGISwapChainResizeTarget)(pNewTargetParameters);
    if (createRenderTargets)
    {
        inst->_ResetRenderState(inst->_CreateRenderTargets(_this)
            ? OverlayHookState::Ready
            : OverlayHookState::Removing);
    }

    return r;
}

HRESULT STDMETHODCALLTYPE DX11Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain1::Present1");
    auto inst = DX11Hook_t::Inst();
    inst->_PrepareForOverlay(_this, Flags);
    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

DX11Hook_t::DX11Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _UsesDXVK(false),
    _DeviceReleasing(0),
    _Device(nullptr),
    _HookDeviceRefCount(0),
    _HookState(OverlayHookState::Removing),
    _DeviceContext(nullptr),
    _RenderTargetView(nullptr),
    _ImGuiFontAtlas(nullptr),
    _ID3D11DeviceRelease(nullptr),
    _IDXGISwapChainPresent(nullptr),
    _IDXGISwapChainResizeBuffers(nullptr),
    _IDXGISwapChainResizeTarget(nullptr),
    _IDXGISwapChain1Present1(nullptr)
{
}

DX11Hook_t::~DX11Hook_t()
{
    INGAMEOVERLAY_INFO("DX11 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

DX11Hook_t* DX11Hook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new DX11Hook_t;

    return _Instance;
}

const char* DX11Hook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t DX11Hook_t::GetRendererHookType() const
{
    return RendererHookType_t::DirectX11;
}

void DX11Hook_t::SetDXVK()
{
    if (!_UsesDXVK)
    {
        _UsesDXVK = true;
        LibraryName += " (DXVK)";
    }
}

void DX11Hook_t::LoadFunctions(
    decltype(_ID3D11DeviceRelease) releaseFcn,
    decltype(_IDXGISwapChainPresent) presentFcn,
    decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
    decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
    decltype(_IDXGISwapChain1Present1) present1Fcn)
{
    _ID3D11DeviceRelease = releaseFcn;

    _IDXGISwapChainPresent = presentFcn;
    _IDXGISwapChainResizeBuffers = resizeBuffersFcn;
    _IDXGISwapChainResizeTarget = resizeTargetFcn;

    _IDXGISwapChain1Present1 = present1Fcn;
}

std::weak_ptr<RendererTexture_t> DX11Hook_t::AllocImageResource()
{
    auto ptr = std::shared_ptr<RendererTexture_t>(new RendererTexture_t(), [](RendererTexture_t* handle)
    {
        if (handle != nullptr)
        {
            auto* resource = reinterpret_cast<ID3D11ShaderResourceView*>(handle->ImGuiTextureId);
            SafeRelease(resource);
            delete handle;
        }
    });

    _ImageResources.emplace(ptr);

    return ptr;
}

void DX11Hook_t::LoadImageResource(RendererTextureLoadParameter_t& loadParameter)
{
    _ImageResourcesToLoad.emplace_back(loadParameter);
}

void DX11Hook_t::ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
        {
            _ImageResources.erase(it);
            _ImageResourcesToRelease.emplace_back(RendererTextureReleaseParameter_t
            {
                std::move(ptr),
                _CurrentFrame
            });
        }
    }
}

}// namespace InGameOverlay