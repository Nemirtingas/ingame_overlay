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

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX10Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
} } while(0)

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX10Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

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

static InGameOverlay::ScreenshotBufferFormat_t D3DFormatToScreenshotFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:            return InGameOverlay::ScreenshotBufferFormat_t::A8R8G8B8;
        case DXGI_FORMAT_B8G8R8A8_UNORM:            return InGameOverlay::ScreenshotBufferFormat_t::B8G8R8A8;
        case DXGI_FORMAT_B8G8R8X8_UNORM:            return InGameOverlay::ScreenshotBufferFormat_t::B8G8R8X8;
        case DXGI_FORMAT_R10G10B10A2_UNORM:         return InGameOverlay::ScreenshotBufferFormat_t::R10G10B10A2;
        case DXGI_FORMAT_B5G6R5_UNORM:              return InGameOverlay::ScreenshotBufferFormat_t::B5G6R5;
        case DXGI_FORMAT_B5G5R5A1_UNORM:            return InGameOverlay::ScreenshotBufferFormat_t::B5G5R5A1;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:        return InGameOverlay::ScreenshotBufferFormat_t::R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_UNORM:        return InGameOverlay::ScreenshotBufferFormat_t::R16G16B16A16_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:       return InGameOverlay::ScreenshotBufferFormat_t::R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:       return InGameOverlay::ScreenshotBufferFormat_t::B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:       return InGameOverlay::ScreenshotBufferFormat_t::B8G8R8X8_UNORM_SRGB;
        default:                                    return InGameOverlay::ScreenshotBufferFormat_t::Unknown;
    }
}

bool DX10Hook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_ID3D10DeviceRelease == nullptr || _IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook DirectX 11: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(ID3D10DeviceRelease);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainPresent);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeTarget);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeBuffers);

        if (_IDXGISwapChain1Present1 != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChain1Present1);

        EndHook();

        INGAMEOVERLAY_INFO("Hooked DirectX 10");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
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

    // Happens when the functions have been hooked, but the DX hook is not setup yet.
    if (_Device == nullptr)
        return false;

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

    if (state == OverlayHookState::Removing)
        ++_DeviceReleasing;

    OverlayHookReady(state);

    _HookState = state;
    _UpdateHookDeviceRefCount();
    switch (state)
    {
        case OverlayHookState::Removing:
            ImGui_ImplDX10_Shutdown();
            WindowsHook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();
            _DestroyRenderTargets();
            SafeRelease(_Device);
            break;

        case OverlayHookState::Reset:
            _DestroyRenderTargets();

    }

    if (state == OverlayHookState::Removing)
        --_DeviceReleasing;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX10Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST)
        return;

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

        _ResetRenderState(OverlayHookState::Ready);
    }

    if (ImGui_ImplDX10_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(desc.OutputWindow))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot(pSwapChain);

        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();

        ImGui::Render();

        _Device->OMSetRenderTargets(1, &_RenderTargetView, nullptr);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot(pSwapChain);
    }
}

void DX10Hook_t::_HandleScreenshot(IDXGISwapChain* pSwapChain)
{
    InGameOverlay::ScreenshotData_t screenshotData;
    if (_CaptureScreenshot(pSwapChain, screenshotData))
        _SendScreenshot(&screenshotData);
    else
        _SendScreenshot(nullptr);
}

bool DX10Hook_t::_CaptureScreenshot(IDXGISwapChain* pSwapChain, ScreenshotData_t& outData)
{
    bool result = false;
    ID3D10Texture2D* backBuffer = nullptr;
    ID3D10Texture2D* stagingTexture = nullptr;
    D3D10_TEXTURE2D_DESC desc;
    D3D10_MAPPED_TEXTURE2D mappedResource;

    HRESULT hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || backBuffer == nullptr)
        goto cleanup;

    backBuffer->GetDesc(&desc);

    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    hr = _Device->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr) || stagingTexture == nullptr)
        goto cleanup;

    _Device->CopyResource(stagingTexture, backBuffer);

    hr = stagingTexture->Map(0, D3D10_MAP_READ, 0, &mappedResource);
    if (FAILED(hr))
        goto cleanup;

    UINT bytesPerPixel = 4;
    UINT rowSize = desc.Width * bytesPerPixel;
    UINT dataSize = desc.Height * rowSize;
    outData.Buffer.resize(dataSize);

    BYTE* src = static_cast<BYTE*>(mappedResource.pData);
    BYTE* dest = outData.Buffer.data();

    for (UINT i = 0; i < desc.Height; i++)
        memcpy(dest + i * rowSize, src + i * mappedResource.RowPitch, rowSize);

    outData.Width = desc.Width;
    outData.Height = desc.Height;
    outData.Format = D3DFormatToScreenshotFormat(desc.Format);

    stagingTexture->Unmap(0);

    result = true;

cleanup:

    SafeRelease(stagingTexture);
    SafeRelease(backBuffer);

    return result;
}

ULONG STDMETHODCALLTYPE DX10Hook_t::_MyID3D10DeviceRelease(ID3D10Device* _this)
{
    auto inst = DX10Hook_t::Inst();
    auto result = (_this->*inst->_ID3D10DeviceRelease)();

    INGAMEOVERLAY_INFO("ID3D10Device::Release: RefCount = {}, Our removal threshold = {}", result, inst->_HookDeviceRefCount);

    if (inst->_DeviceReleasing == 0 && _this == inst->_Device && result < inst->_HookDeviceRefCount)
        inst->_ResetRenderState(OverlayHookState::Removing);

    return result;
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::Present");
    auto inst = DX10Hook_t::Inst();
    inst->_PrepareForOverlay(_this, Flags);
    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeBuffers");
    auto inst = DX10Hook_t::Inst();
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

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeTarget");
    auto inst = DX10Hook_t::Inst();
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

HRESULT STDMETHODCALLTYPE DX10Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain1::Present1");
    auto inst = DX10Hook_t::Inst();
    inst->_PrepareForOverlay(_this, Flags);
    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

DX10Hook_t::DX10Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _UsesDXVK(false),
    _DeviceReleasing(0),
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
    INGAMEOVERLAY_INFO("DX10 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

DX10Hook_t* DX10Hook_t::Inst()
{
    if (_Instance == nullptr)   
        _Instance = new DX10Hook_t;

    return _Instance;
}

const char* DX10Hook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t DX10Hook_t::GetRendererHookType() const
{
    return RendererHookType_t::DirectX10;
}

void DX10Hook_t::SetDXVK()
{
    if (!_UsesDXVK)
    {
        _UsesDXVK = true;
        LibraryName += " (DXVK)";
    }
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