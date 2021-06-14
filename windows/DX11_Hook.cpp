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

#include "DX11_Hook.h"
#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

DX11_Hook* DX11_Hook::_inst = nullptr;

HRESULT GetDeviceAndCtxFromSwapchain(IDXGISwapChain* pSwapChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext)
{
    HRESULT ret = pSwapChain->GetDevice(IID_PPV_ARGS(ppDevice));

    if (SUCCEEDED(ret))
        (*ppDevice)->GetImmediateContext(ppContext);

    return ret;
}

bool DX11_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (Present == nullptr || ResizeTarget == nullptr || ResizeBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 11: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->start_hook(key_combination_callback))
            return false;

        windows_hooked = true;

        SPDLOG_INFO("Hooked DirectX 11");
        hooked = true;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)Present      , &DX11_Hook::MyPresent),
            std::make_pair<void**, void*>(&(PVOID&)ResizeTarget , &DX11_Hook::MyResizeTarget),
            std::make_pair<void**, void*>(&(PVOID&)ResizeBuffers, &DX11_Hook::MyResizeBuffers)
        );
        EndHook();
    }
    return true;
}

bool DX11_Hook::is_started()
{
    return hooked;
}

void DX11_Hook::resetRenderState()
{
    if (initialized)
    {
        overlay_hook_ready(false);

        ImGui_ImplDX11_Shutdown();
        Windows_Hook::Inst()->resetRenderState();
        //ImGui::DestroyContext();

        if (mainRenderTargetView)
        {
            mainRenderTargetView->Release();
            mainRenderTargetView = nullptr;
        }

        pContext->Release();
        pDevice->Release();

        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX11_Hook::prepareForOverlay(IDXGISwapChain* pSwapChain)
{
    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);

    if (!initialized)
    {
        pDevice = nullptr;
        if (FAILED(GetDeviceAndCtxFromSwapchain(pSwapChain, &pDevice, &pContext)))
            return;

        ID3D11Texture2D* pBackBuffer;
        pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));

        pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);

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

        if (mainRenderTargetView == nullptr)
            return;

        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext();

        ImGui_ImplDX11_Init(pDevice, pContext);

        initialized = true;
        overlay_hook_ready(true);
    }

    if (ImGui_ImplDX11_NewFrame() && Windows_Hook::Inst()->prepareForOverlay(desc.OutputWindow))
    {
        ImGui::NewFrame();

        overlay_proc();

        ImGui::Render();

        if (mainRenderTargetView)
            pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

HRESULT STDMETHODCALLTYPE DX11_Hook::MyPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    auto inst = DX11_Hook::Inst();
    inst->prepareForOverlay(_this);
    return (_this->*inst->Present)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX11_Hook::MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    auto inst = DX11_Hook::Inst();
    inst->resetRenderState();
    return (_this->*inst->ResizeTarget)(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DX11_Hook::MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto inst = DX11_Hook::Inst();
    inst->resetRenderState();
    return (_this->*inst->ResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

DX11_Hook::DX11_Hook():
    initialized(false),
    hooked(false),
    windows_hooked(false),
    pContext(nullptr),
    mainRenderTargetView(nullptr),
    Present(nullptr),
    ResizeBuffers(nullptr),
    ResizeTarget(nullptr)
{
    _library = LoadLibraryA(DLL_NAME);
}

DX11_Hook::~DX11_Hook()
{
    SPDLOG_INFO("DX11 Hook removed");

    if (windows_hooked)
        delete Windows_Hook::Inst();

    if (initialized)
    {
        if (mainRenderTargetView)
            mainRenderTargetView->Release();

        pContext->Release();

        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui::DestroyContext();

        initialized = false;
    }

    FreeLibrary(reinterpret_cast<HMODULE>(_library));

    _inst = nullptr;
}

DX11_Hook* DX11_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new DX11_Hook;

    return _inst;
}

const char* DX11_Hook::get_lib_name() const
{
    return DLL_NAME;
}

void DX11_Hook::loadFunctions(decltype(Present) PresentFcn, decltype(ResizeBuffers) ResizeBuffersFcn, decltype(ResizeTarget) ResizeTargetFcn)
{
    Present = PresentFcn;
    ResizeBuffers = ResizeBuffersFcn;
    ResizeTarget = ResizeTargetFcn;
}

std::weak_ptr<uint64_t> DX11_Hook::CreateImageResource(std::shared_ptr<Image> source)
{
    ID3D11ShaderResourceView** resource = new ID3D11ShaderResourceView*(nullptr);

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(source->width());
    desc.Height = static_cast<UINT>(source->height());
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = nullptr;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = source->get_raw_pointer();
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    pDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    if (pTexture != nullptr)
    {
        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        pDevice->CreateShaderResourceView(pTexture, &srvDesc, resource);
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

    image_resources.emplace(ptr);

    return ptr;
}

void DX11_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = image_resources.find(ptr);
        if (it != image_resources.end())
            image_resources.erase(it);
    }
}