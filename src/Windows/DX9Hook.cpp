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

#include <imgui.h>
#include <backends/imgui_impl_dx9.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX9Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
} } while(0)

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX9Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

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

static InGameOverlay::ScreenshotDataFormat_t RendererFormatToScreenshotFormat(D3DFORMAT format)
{
    switch (format)
    {
        case D3DFMT_R8G8B8     : return InGameOverlay::ScreenshotDataFormat_t::R8G8B8;
        case D3DFMT_X8R8G8B8   : return InGameOverlay::ScreenshotDataFormat_t::X8R8G8B8;
        case D3DFMT_A8R8G8B8   : return InGameOverlay::ScreenshotDataFormat_t::A8R8G8B8;
        case D3DFMT_R5G6B5     : return InGameOverlay::ScreenshotDataFormat_t::R5G6B5;
        case D3DFMT_X1R5G5B5   : return InGameOverlay::ScreenshotDataFormat_t::X1R5G5B5;
        case D3DFMT_A1R5G5B5   : return InGameOverlay::ScreenshotDataFormat_t::A1R5G5B5;
        case D3DFMT_A2R10G10B10: return InGameOverlay::ScreenshotDataFormat_t::A2R10G10B10;
        case D3DFMT_A2B10G10R10: return InGameOverlay::ScreenshotDataFormat_t::A2B10G10R10;
        default:                 return InGameOverlay::ScreenshotDataFormat_t::Unknown;
    }
}

bool DX9Hook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_IDirect3DDevice9Release == nullptr || _IDirect3DDevice9Reset == nullptr || _IDirect3DDevice9Present == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook DirectX 9: Rendering functions missing.");
            return false;
        }
        
        if (!WindowsHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(IDirect3DDevice9Release);
        TRY_HOOK_FUNCTION_OR_FAIL(IDirect3DDevice9Reset);
        TRY_HOOK_FUNCTION_OR_FAIL(IDirect3DDevice9Present);
        if (_IDirect3DDevice9ExPresentEx != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDirect3DDevice9ExPresentEx);

        if (_IDirect3DDevice9ExResetEx != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDirect3DDevice9ExResetEx);

        if (_IDirect3DSwapChain9SwapChainPresent != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDirect3DSwapChain9SwapChainPresent);

        EndHook();

        INGAMEOVERLAY_INFO("Hooked DirectX 9");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void DX9Hook_t::HideAppInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideAppInputs(hide);
}

void DX9Hook_t::HideOverlayInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
}

bool DX9Hook_t::IsStarted()
{
    return _Hooked;
}

void DX9Hook_t::_UpdateHookDeviceRefCount()
{
    constexpr ULONG BaseRefCount = 2;

    switch (_HookState)
    {
        // 0 ref from ImGui
        case OverlayHookState::Removing: _HookDeviceRefCount = BaseRefCount; break;
        // 1 ref from us, 1 ref from ImGui (device)
        case OverlayHookState::Reset: _HookDeviceRefCount = BaseRefCount + 1; break;
        // 1 ref from us, 4 refs from ImGui (device, vertex buffer, index buffer, font texture)
        case OverlayHookState::Ready: _HookDeviceRefCount = BaseRefCount + 4 + _ImageResources.size();
    }
}

void DX9Hook_t::_ResetRenderState(OverlayHookState state)
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
        case OverlayHookState::Reset:
            ImGui_ImplDX9_InvalidateDeviceObjects();
            // Yes, clearing images is required when resetting or DirectX9 will return a D3DERR_INVALIDCALL error
            _ImageResources.clear();
            break;

        case OverlayHookState::Removing:
            ImGui_ImplDX9_Shutdown();
            WindowsHook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();
            SafeRelease(_Device);

            _LastWindow = nullptr;
    }

    if (state == OverlayHookState::Removing)
        --_DeviceReleasing;
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
                destWindow = params.hDeviceWindow;

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
    if (destWindow != _LastWindow || _Device != pDevice)
        _ResetRenderState(OverlayHookState::Removing);

    if (_HookState == OverlayHookState::Removing)
    {
        _Device = pDevice;
        _Device->AddRef();

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX9_Init(pDevice);

        _LastWindow = destWindow;

        WindowsHook_t::Inst()->SetInitialWindowSize(destWindow);

        if (!ImGui_ImplDX9_CreateDeviceObjects())
        {
            ImGui_ImplDX9_Shutdown();
            SafeRelease(_Device);
            return;
        }

        _ResetRenderState(OverlayHookState::Ready);
    }

    if (_HookState != OverlayHookState::Ready)
        return;

    if (ImGui_ImplDX9_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(destWindow))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot();

        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();

        ImGui::Render();

        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot();
    }
}

void DX9Hook_t::_HandleScreenshot()
{
    bool result = false;
    IDirect3DSurface9* backBuffer = nullptr;
    HRESULT hr = _Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr) || backBuffer == nullptr)
        goto cleanup;

    D3DSURFACE_DESC desc;
    backBuffer->GetDesc(&desc);

    IDirect3DSurface9* cpuSurface = nullptr;
    hr = _Device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &cpuSurface, nullptr);

    if (FAILED(hr) || cpuSurface == nullptr)
        goto cleanup;

    hr = _Device->GetRenderTargetData(backBuffer, cpuSurface);
    if (FAILED(hr))
        goto cleanup;

    D3DLOCKED_RECT lockedRect;
    hr = cpuSurface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr))
        goto cleanup;

    ScreenshotCallbackParameter_t screenshot;
    screenshot.Width = desc.Width;
    screenshot.Height = desc.Height;
    screenshot.Pitch = lockedRect.Pitch;
    screenshot.Data = reinterpret_cast<void*>(lockedRect.pBits);
    screenshot.Format = RendererFormatToScreenshotFormat(desc.Format);

    _SendScreenshot(&screenshot);

    cpuSurface->UnlockRect();

    result = true;

cleanup:

    SafeRelease(cpuSurface);
    SafeRelease(backBuffer);

    if (!result)
        _SendScreenshot(nullptr);
}

ULONG STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9Release(IDirect3DDevice9* _this)
{
    auto inst = DX9Hook_t::Inst();
    auto result = (_this->*inst->_IDirect3DDevice9Release)();

    INGAMEOVERLAY_INFO("IDirect3DDevice9::Release: RefCount = {}, Our removal threshold = {}", result, inst->_HookDeviceRefCount);

    if (inst->_DeviceReleasing == 0 && _this == inst->_Device && result < inst->_HookDeviceRefCount)
        inst->_ResetRenderState(OverlayHookState::Removing);

    return result;
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9Reset(IDirect3DDevice9* _this, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    INGAMEOVERLAY_INFO("IDirect3DDevice9::Reset");
    auto inst = DX9Hook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_Device != nullptr && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = true;
        inst->_ResetRenderState(OverlayHookState::Reset);
    }
    auto r = (_this->*inst->_IDirect3DDevice9Reset)(pPresentationParameters);
    if (createRenderTargets)
    {
        inst->_ResetRenderState(ImGui_ImplDX9_CreateDeviceObjects()
            ? OverlayHookState::Ready
            : OverlayHookState::Removing);
    }

    return r;
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    INGAMEOVERLAY_INFO("IDirect3DDevice9::Present");
    auto inst = DX9Hook_t::Inst();
    inst->_PrepareForOverlay(_this, hDestWindowOverride);
    return (_this->*inst->_IDirect3DDevice9Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9ExPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    INGAMEOVERLAY_INFO("IDirect3DDevice9Ex::PresentEx");
    auto inst = DX9Hook_t::Inst();
    inst->_PrepareForOverlay(_this, hDestWindowOverride);
    return (_this->*inst->_IDirect3DDevice9ExPresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DDevice9ExResetEx(IDirect3DDevice9Ex* _this, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
    INGAMEOVERLAY_INFO("IDirect3DDevice9Ex::ResetEx");
    auto inst = DX9Hook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_Device != nullptr && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = true;
        inst->_ResetRenderState(OverlayHookState::Reset);
    }
    auto r = (_this->*inst->_IDirect3DDevice9ExResetEx)(pPresentationParameters, pFullscreenDisplayMode);
    if (createRenderTargets)
    {
        inst->_ResetRenderState(ImGui_ImplDX9_CreateDeviceObjects()
            ? OverlayHookState::Ready
            : OverlayHookState::Removing);
    }

    return r;
}

HRESULT STDMETHODCALLTYPE DX9Hook_t::_MyIDirect3DSwapChain9SwapChainPresent(IDirect3DSwapChain9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    INGAMEOVERLAY_INFO("IDirect3DSwapChain9::Present");
    IDirect3DDevice9* pDevice;
    auto inst = DX9Hook_t::Inst();

    if (SUCCEEDED(_this->GetDevice(&pDevice)))
    {
        HWND destWindow = hDestWindowOverride;
        if (!destWindow)
        {
            D3DPRESENT_PARAMETERS param;
            if (_this->GetPresentParameters(&param) == D3D_OK)
                destWindow = param.hDeviceWindow;
        }

        inst->_PrepareForOverlay(pDevice, destWindow);
        pDevice->Release();
    }
    return (_this->*inst->_IDirect3DSwapChain9SwapChainPresent)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

DX9Hook_t::DX9Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _UsesDXVK(false),
    _LastWindow(nullptr),
    _DeviceReleasing(0),
    _Device(nullptr),
    _HookDeviceRefCount(0),
    _HookState(OverlayHookState::Removing),
    _ImGuiFontAtlas(nullptr),
    _IDirect3DDevice9Release(nullptr),
    _IDirect3DDevice9Present(nullptr),
    _IDirect3DDevice9Reset(nullptr),
    _IDirect3DDevice9ExPresentEx(nullptr),
    _IDirect3DDevice9ExResetEx(nullptr),
    _IDirect3DSwapChain9SwapChainPresent(nullptr)
{
}

DX9Hook_t::~DX9Hook_t()
{
    INGAMEOVERLAY_INFO("DX9 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

DX9Hook_t* DX9Hook_t::Inst()
{
    if( _Instance == nullptr )
        _Instance = new DX9Hook_t;

    return _Instance;
}

const char* DX9Hook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t DX9Hook_t::GetRendererHookType() const
{
    return RendererHookType_t::DirectX9;
}

void DX9Hook_t::SetDXVK()
{
    if (!_UsesDXVK)
    {
        _UsesDXVK = true;
        LibraryName += " (DXVK)";
    }
}

void DX9Hook_t::LoadFunctions(
    decltype(_IDirect3DDevice9Release) ReleaseFcn,
    decltype(_IDirect3DDevice9Present) PresentFcn,
    decltype(_IDirect3DDevice9Reset) ResetFcn,
    decltype(_IDirect3DDevice9ExPresentEx) PresentExFcn,
    decltype(_IDirect3DDevice9ExResetEx) ResetExFcn,
    decltype(_IDirect3DSwapChain9SwapChainPresent) SwapChainPresentFcn)
{
    _IDirect3DDevice9Release = ReleaseFcn;
    _IDirect3DDevice9Present = PresentFcn;
    _IDirect3DDevice9Reset = ResetFcn;

    _IDirect3DDevice9ExPresentEx = PresentExFcn;
    _IDirect3DDevice9ExResetEx = ResetExFcn;

    _IDirect3DSwapChain9SwapChainPresent = SwapChainPresentFcn;
}

std::weak_ptr<uint64_t> DX9Hook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    IDirect3DTexture9** pTexture = new IDirect3DTexture9*(nullptr);

    _Device->CreateTexture(
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

    _UpdateHookDeviceRefCount();

    return ptr;
}

void DX9Hook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
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