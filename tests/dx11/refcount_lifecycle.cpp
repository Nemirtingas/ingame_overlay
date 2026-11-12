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
#include "DirectXVTables.h"
#include <InGameOverlay/RendererDetector.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>

using namespace InGameOverlay;

namespace
{
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain1* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_hostRenderTarget = nullptr;
    ImGuiContext* g_hostContext = nullptr;
    ImGuiContext* g_overlayContext = nullptr;
    int g_readyCount = 0;
    int g_removingCount = 0;
    int g_overlayFrameCount = 0;
    int g_overlayGeometryFrameCount = 0;
    bool g_overlayGeometry = false;

    bool Check(HRESULT result, const char* expression, int line)
    {
        if (SUCCEEDED(result))
            return true;

        std::printf("FAIL line=%d %s HRESULT=0x%08lx\n", line, expression, static_cast<unsigned long>(result));
        return false;
    }

#define CHECK_HR(expression) do { if (!Check((expression), #expression, __LINE__)) return false; } while (false)

    bool CreateHost(HWND window)
    {
        D3D_FEATURE_LEVEL featureLevel{};
        CHECK_HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &g_device, &featureLevel, &g_context));

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        CHECK_HR(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));
        CHECK_HR(dxgiDevice->GetAdapter(&adapter));
        CHECK_HR(adapter->GetParent(IID_PPV_ARGS(&factory)));

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = 64;
        description.Height = 64;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        CHECK_HR(factory->CreateSwapChainForHwnd(g_device, window, &description, nullptr, nullptr, &g_swapChain));

        ID3D11Texture2D* backBuffer = nullptr;
        CHECK_HR(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)));
        CHECK_HR(g_device->CreateRenderTargetView(backBuffer, nullptr, &g_hostRenderTarget));
        backBuffer->Release();
        factory->Release();
        adapter->Release();
        dxgiDevice->Release();
        std::printf("WARP device created feature=%u\n", static_cast<unsigned>(featureLevel));
        return true;
    }

    void HookStateChanged(OverlayHookState state)
    {
        if (state == OverlayHookState::Ready)
        {
            ++g_readyCount;
            g_overlayContext = ImGui::GetCurrentContext();
            std::printf("callback Ready context=%p host_context=%p\n", static_cast<void*>(g_overlayContext), static_cast<void*>(g_hostContext));
        }
        else if (state == OverlayHookState::Removing)
        {
            ++g_removingCount;
            std::printf("callback Removing before FreeDetector count=%d\n", g_removingCount);
        }
    }

    bool PresentFrame(bool withGeometry)
    {
        ImGui::SetCurrentContext(g_hostContext);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (withGeometry)
        {
            ImGui::Begin("host ImGui context");
            ImGui::TextUnformatted("host-owned draw data");
            ImGui::End();
        }
        ImGui::Render();

        const float clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_hostRenderTarget, nullptr);
        g_context->ClearRenderTargetView(g_hostRenderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present uses the overlay context; the host context is restored afterward.
        ImGui::SetCurrentContext(g_overlayContext);
        const DXGI_PRESENT_PARAMETERS parameters{};
        const HRESULT presentResult = g_swapChain->Present1(0, 0, &parameters);
        ImGui::SetCurrentContext(g_hostContext);
        return Check(presentResult, "g_swapChain->Present1", __LINE__);
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t className[] = L"InGameOverlayDX11LifecycleTest";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0)
    {
        std::printf("FAIL RegisterClassExW error=%lu\n", GetLastError());
        return 1;
    }

    HWND window = CreateWindowExW(0, className, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (window == nullptr)
    {
        std::printf("FAIL CreateWindowExW error=%lu\n", GetLastError());
        UnregisterClassW(className, windowClass.hInstance);
        return 1;
    }

    if (!CreateHost(window))
        return 1;

    IMGUI_CHECKVERSION();
    g_hostContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_hostContext);
    if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(g_device, g_context))
    {
        std::printf("FAIL initialize host ImGui backends\n");
        return 1;
    }

    auto** deviceVTable = *reinterpret_cast<void***>(g_device);
    auto** swapChainVTable = *reinterpret_cast<void***>(g_swapChain);
    decltype(&ID3D11Device::Release) releaseFunction = nullptr;
    decltype(&IDXGISwapChain::Present) presentFunction = nullptr;
    decltype(&IDXGISwapChain::ResizeBuffers) resizeBuffersFunction = nullptr;
    decltype(&IDXGISwapChain::ResizeTarget) resizeTargetFunction = nullptr;
    decltype(&IDXGISwapChain1::Present1) present1Function = nullptr;
    reinterpret_cast<void*&>(releaseFunction) = deviceVTable[static_cast<int>(ID3D11DeviceVTable::Release)];
    reinterpret_cast<void*&>(presentFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::Present)];
    reinterpret_cast<void*&>(resizeBuffersFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::ResizeBuffers)];
    reinterpret_cast<void*&>(resizeTargetFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::ResizeTarget)];
    reinterpret_cast<void*&>(present1Function) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::Present1)];

    auto* hook = DX11Hook_t::Inst();
    hook->LoadFunctions(releaseFunction, presentFunction, resizeBuffersFunction, resizeTargetFunction, present1Function);
    hook->OverlayHookReady = HookStateChanged;
    hook->OverlayProc = []
    {
        ++g_overlayFrameCount;
        if (ImGui::GetCurrentContext() == g_hostContext)
            std::printf("FAIL overlay used host ImGui context\n");
        if (g_overlayGeometry)
        {
            ++g_overlayGeometryFrameCount;
            ImGui::Begin("overlay context");
            ImGui::TextUnformatted("overlay-owned draw data");
            ImGui::End();
        }
    };

    ToggleKey toggleKey = ToggleKey::F1;
    if (!hook->StartHook([] {}, &toggleKey, 1))
    {
        std::printf("FAIL StartHook\n");
        return 1;
    }
    std::printf("StartHook succeeded\n");

    g_overlayGeometry = false;
    for (int frame = 0; frame < 2; ++frame)
        if (!PresentFrame(false)) return 1;
    g_overlayGeometry = true;
    for (int frame = 0; frame < 3; ++frame)
        if (!PresentFrame(true)) return 1;

    if (g_readyCount != 1 || g_overlayFrameCount < 5 || g_overlayGeometryFrameCount != 3
        || g_overlayContext == nullptr || g_overlayContext == g_hostContext)
    {
        std::printf("FAIL before host teardown ready=%d overlay_frames=%d geometry_frames=%d overlay_context=%p host_context=%p\n",
            g_readyCount, g_overlayFrameCount, g_overlayGeometryFrameCount,
            static_cast<void*>(g_overlayContext), static_cast<void*>(g_hostContext));
        return 2;
    }
    if (g_removingCount != 0)
    {
        std::printf("FAIL Removing while host is active count=%d\n", g_removingCount);
        return 2;
    }
    std::printf("host active: Ready=%d overlay_frames=%d geometry_frames=%d removing=%d\n",
        g_readyCount, g_overlayFrameCount, g_overlayGeometryFrameCount, g_removingCount);

    ImGui::SetCurrentContext(g_hostContext);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_hostContext);
    g_hostContext = nullptr;
    ImGui::SetCurrentContext(g_overlayContext);

    g_hostRenderTarget->Release();
    g_hostRenderTarget = nullptr;
    g_swapChain->Release();
    g_swapChain = nullptr;
    g_context->Release();
    g_context = nullptr;
    std::printf("host ImGui, RTV, swapchain, and immediate context released; Removing=%d\n", g_removingCount);

    const ULONG finalDeviceReleaseCount = g_device->Release();
    g_device = nullptr;
    std::printf("host device Release returned=%lu; callback Removing=%d\n",
        static_cast<unsigned long>(finalDeviceReleaseCount), g_removingCount);

    const bool removedBeforeDetectorFree = g_removingCount == 1;
    if (!removedBeforeDetectorFree)
        std::printf("FAIL Removing callback did not run on the host device release\n");

    FreeDetector();
    const bool removingStayedSingle = g_removingCount == 1;
    DestroyWindow(window);
    UnregisterClassW(className, windowClass.hInstance);

    if (!removedBeforeDetectorFree || !removingStayedSingle)
        return 3;
    std::printf("PASS Removing preceded FreeDetector; Ready=%d overlay_frames=%d geometry_frames=%d\n",
        g_readyCount, g_overlayFrameCount, g_overlayGeometryFrameCount);
    return 0;
}
