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
// Direct3D 9 has no software device: this test needs a display adapter.

#include "DX9Hook.h"
#include "DirectXVTables.h"
#include <InGameOverlay/RendererDetector.h>

#include <imgui.h>

#include <Windows.h>
#include <d3d9.h>
#include <cstdio>

using namespace InGameOverlay;

namespace
{
    IDirect3D9* g_d3d = nullptr;
    IDirect3DDevice9* g_device = nullptr;
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

    bool CreateApplication(HWND window)
    {
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.BackBufferWidth = 64;
        parameters.BackBufferHeight = 64;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        CHECK_HR(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &g_device));
        return true;
    }

    bool PresentFrames(int count)
    {
        for (int frame = 0; frame < count; ++frame)
            CHECK_HR(g_device->Present(nullptr, nullptr, nullptr, nullptr));
        return true;
    }

    bool RunScenario(const char* name, int width, int height)
    {
        const wchar_t className[] = L"InGameOverlayDX9NoHostTest";
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
        IDirect3DSurface9* backBuffer = nullptr;
        CHECK_HR(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer));
        backBuffer->Release();
        if (!PresentFrames(2))
            return false;

        if (g_removingCount == expectedRemoving)
        {
            std::printf("FAIL %s Removing while the application holds its device\n", name);
            return false;
        }

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
    const wchar_t className[] = L"InGameOverlayDX9NoHostTest";
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

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    HWND vtableWindow = CreateWindowExW(0, className, L"", WS_POPUP,
        0, 0, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (g_d3d == nullptr || vtableWindow == nullptr || !CreateApplication(vtableWindow))
    {
        std::printf("FAIL create the application used to read the vtables\n");
        return 1;
    }

    auto** deviceVTable = *reinterpret_cast<void***>(g_device);
    decltype(&IDirect3DDevice9::Release) releaseFunction = nullptr;
    decltype(&IDirect3DDevice9::Present) presentFunction = nullptr;
    decltype(&IDirect3DDevice9::Reset) resetFunction = nullptr;
    reinterpret_cast<void*&>(releaseFunction) = deviceVTable[static_cast<int>(IDirect3DDevice9VTable::Release)];
    reinterpret_cast<void*&>(presentFunction) = deviceVTable[static_cast<int>(IDirect3DDevice9VTable::Present)];
    reinterpret_cast<void*&>(resetFunction) = deviceVTable[static_cast<int>(IDirect3DDevice9VTable::Reset)];

    g_device->Release();
    g_device = nullptr;
    DestroyWindow(vtableWindow);

    auto* hook = DX9Hook_t::Inst();
    hook->LoadFunctions(releaseFunction, presentFunction, resetFunction, nullptr, nullptr, nullptr);
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
    g_d3d->Release();
    UnregisterClassW(className, windowClass.hInstance);

    if (!windowed || !zeroSize)
        return 2;

    std::printf("PASS ready=%d removing=%d\n", g_readyCount, g_removingCount);
    return 0;
}
