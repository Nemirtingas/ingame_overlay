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

// A minimal application holds few device references and has no ImGui of its own,
// so nothing hides a removal threshold that is above what the hook really holds.

#include "DX10Hook.h"
#include "DirectXVTables.h"
#include <InGameOverlay/RendererDetector.h>

#include <imgui.h>

#include <Windows.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <cstdio>

using namespace InGameOverlay;

namespace
{
    ID3D10Device* g_device = nullptr;
    IDXGISwapChain1* g_swapChain = nullptr;
    ID3D10RenderTargetView* g_renderTarget = nullptr;
    int g_readyCount = 0;
    int g_removingCount = 0;

    bool Check(HRESULT result, const char* expression, int line)
    {
        if (SUCCEEDED(result))
            return true;

        std::printf("FAIL line=%d %s HRESULT=0x%08lx\n", line, expression, static_cast<unsigned long>(result));
        return false;
    }

#define CHECK_HR(expression) do { if (!Check((expression), #expression, __LINE__)) return false; } while (false)

    bool CreateRenderTarget()
    {
        ID3D10Texture2D* backBuffer = nullptr;
        CHECK_HR(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)));
        const HRESULT result = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
        CHECK_HR(result);
        return true;
    }

    bool CreateApplication(HWND window)
    {
        CHECK_HR(D3D10CreateDevice(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, 0, D3D10_SDK_VERSION, &g_device));

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

        factory->Release();
        adapter->Release();
        dxgiDevice->Release();

        return CreateRenderTarget();
    }

    bool PresentFrames(int count)
    {
        for (int frame = 0; frame < count; ++frame)
        {
            g_device->OMSetRenderTargets(1, &g_renderTarget, nullptr);
            CHECK_HR(g_swapChain->Present(0, 0));
        }
        return true;
    }

    void ReleaseRenderTarget()
    {
        g_device->OMSetRenderTargets(0, nullptr, nullptr);
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }

    bool RunScenario(const char* name, int width, int height)
    {
        const wchar_t className[] = L"InGameOverlayDX10NoHostTest";
        HWND window = CreateWindowExW(0, className, L"", WS_POPUP,
            0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window == nullptr)
        {
            std::printf("FAIL %s CreateWindowExW error=%lu\n", name, GetLastError());
            return false;
        }

        if (!CreateApplication(window))
            return false;

        const int expectedReady = g_readyCount + 1;
        const int expectedRemoving = g_removingCount + 1;

        if (!PresentFrames(5))
            return false;

        if (g_readyCount != expectedReady || g_removingCount == expectedRemoving)
        {
            std::printf("FAIL %s while presenting ready=%d removing=%d\n", name, g_readyCount, g_removingCount);
            return false;
        }

        // Any device release from the application must leave the overlay alone while it holds the device.
        g_device->AddRef();
        g_device->Release();
        ReleaseRenderTarget();
        if (!CreateRenderTarget() || !PresentFrames(2))
            return false;

        if (g_removingCount == expectedRemoving)
        {
            std::printf("FAIL %s Removing while the application holds its device\n", name);
            return false;
        }

        ReleaseRenderTarget();
        g_swapChain->Release();
        g_swapChain = nullptr;
        const ULONG deviceRefCount = g_device->Release();
        g_device = nullptr;

        std::printf("%s: last application device release returned=%lu ready=%d removing=%d\n",
            name, static_cast<unsigned long>(deviceRefCount), g_readyCount, g_removingCount);

        DestroyWindow(window);

        if (g_removingCount != expectedRemoving)
        {
            std::printf("FAIL %s Removing did not run on the last application device release\n", name);
            return false;
        }
        return true;
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t className[] = L"InGameOverlayDX10NoHostTest";
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

    HWND vtableWindow = CreateWindowExW(0, className, L"", WS_POPUP,
        0, 0, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (vtableWindow == nullptr || !CreateApplication(vtableWindow))
    {
        std::printf("FAIL create the application used to read the vtables\n");
        return 1;
    }

    auto** deviceVTable = *reinterpret_cast<void***>(g_device);
    auto** swapChainVTable = *reinterpret_cast<void***>(g_swapChain);
    decltype(&ID3D10Device::Release) releaseFunction = nullptr;
    decltype(&IDXGISwapChain::Present) presentFunction = nullptr;
    decltype(&IDXGISwapChain::ResizeBuffers) resizeBuffersFunction = nullptr;
    decltype(&IDXGISwapChain::ResizeTarget) resizeTargetFunction = nullptr;
    decltype(&IDXGISwapChain1::Present1) present1Function = nullptr;
    reinterpret_cast<void*&>(releaseFunction) = deviceVTable[static_cast<int>(ID3D10DeviceVTable::Release)];
    reinterpret_cast<void*&>(presentFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::Present)];
    reinterpret_cast<void*&>(resizeBuffersFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::ResizeBuffers)];
    reinterpret_cast<void*&>(resizeTargetFunction) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::ResizeTarget)];
    reinterpret_cast<void*&>(present1Function) = swapChainVTable[static_cast<int>(IDXGISwapChainVTable::Present1)];

    ReleaseRenderTarget();
    g_swapChain->Release();
    g_swapChain = nullptr;
    g_device->Release();
    g_device = nullptr;
    DestroyWindow(vtableWindow);

    auto* hook = DX10Hook_t::Inst();
    hook->LoadFunctions(releaseFunction, presentFunction, resizeBuffersFunction, resizeTargetFunction, present1Function);
    hook->OverlayHookReady = [](OverlayHookState state)
    {
        if (state == OverlayHookState::Ready)
            ++g_readyCount;
        else if (state == OverlayHookState::Removing)
            ++g_removingCount;
    };
    hook->OverlayProc = []
    {
        ImGui::Begin("overlay");
        ImGui::TextUnformatted("overlay draw data");
        ImGui::End();
    };

    ToggleKey toggleKey = ToggleKey::F1;
    if (!hook->StartHook([] {}, &toggleKey, 1))
    {
        std::printf("FAIL StartHook\n");
        return 1;
    }

    const bool windowed = RunScenario("windowed", 64, 64);
    // A zero-size window never renders, so the backend never creates its lazy objects.
    const bool zeroSize = windowed && RunScenario("zero-size", 0, 0);

    FreeDetector();
    UnregisterClassW(className, windowClass.hInstance);

    if (!windowed || !zeroSize)
        return 2;

    std::printf("PASS ready=%d removing=%d\n", g_readyCount, g_removingCount);
    return 0;
}
