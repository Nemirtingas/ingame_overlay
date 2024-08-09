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

#include <cassert>

#include <InGameOverlay/RendererDetector.h>

#include <System/Encoding.hpp>
#include <System/String.hpp>
#include <System/System.h>
#include <System/Library.h>
#include <System/ScopedLock.hpp>
#include <mini_detour/mini_detour.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "DX12Hook.h"
#include "DX11Hook.h"
#include "DX10Hook.h"
#include "DX9Hook.h"
#include "OpenGLHook.h"
#include "VulkanHook.h"
  
#include "DirectXVTables.h"
  
#include <random>

#ifdef INGAMEOVERLAY_USE_SPDLOG

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#endif
  
#ifdef GetModuleHandle
    #undef GetModuleHandle
#endif
#ifdef GetSystemDirectory
    #undef GetSystemDirectory
#endif

#define TRY_HOOK_FUNCTION(NAME, HOOK) do { if (!_DetectionHooks.HookFunc(std::make_pair<void**, void*>(&(void*&)NAME, (void*)HOOK))) { \
    SPDLOG_ERROR("Failed to hook {}", #NAME); } } while(0)

namespace InGameOverlay {

static constexpr const char DXGI_DLL_NAME[] = "dxgi.dll";
static constexpr const char DX9_DLL_NAME[] = "d3d9.dll";
static constexpr const char DX10_DLL_NAME[] = "d3d10.dll";
static constexpr const char DX11_DLL_NAME[] = "d3d11.dll";
static constexpr const char DX12_DLL_NAME[] = "d3d12.dll";
static constexpr const char OPENGL_DLL_NAME[] = "opengl32.dll";
static constexpr const char VULKAN_DLL_NAME[] = "vulkan-1.dll";

struct DirectX9Driver_t
{
    std::string LibraryPath;
    decltype(&IDirect3DDevice9::Release) pfnRelease;
    decltype(&IDirect3DDevice9::Present) pfnPresent;
    decltype(&IDirect3DDevice9::Reset) pfnReset;
    decltype(&IDirect3DDevice9Ex::PresentEx) pfnPresentEx;
    decltype(&IDirect3DDevice9Ex::ResetEx) pfnResetEx;
    decltype(&IDirect3DSwapChain9::Present) pfnSwapChainPresent;
};

struct DirectX10Driver_t
{
    std::string LibraryPath;
    decltype(&ID3D10Device::Release) pfnRelease;
    decltype(&IDXGISwapChain::Present) pfnPresent;
    decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;
    decltype(&IDXGISwapChain1::Present1) pfnPresent1;
};

struct DirectX11Driver_t
{
    std::string LibraryPath;
    decltype(&ID3D11Device::Release) pfnRelease;
    decltype(&IDXGISwapChain::Present) pfnPresent;
    decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;
    decltype(&IDXGISwapChain1::Present1) pfnPresent1;
};

struct DirectX12Driver_t
{
    std::string LibraryPath;
    decltype(&ID3D12Device::Release) pfnRelease;
    decltype(&IDXGISwapChain::Present) pfnPresent;
    decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;
    decltype(&IDXGISwapChain1::Present1) pfnPresent1;
    decltype(&IDXGISwapChain3::ResizeBuffers1) pfnResizeBuffer1;
    decltype(&ID3D12CommandQueue::ExecuteCommandLists) pfnExecuteCommandLists;
};

struct OpenGLDriver_t
{
    std::string LibraryPath;
    decltype(::SwapBuffers)* wglSwapBuffers;
};

static std::wstring RandomString(size_t length)
{
    wchar_t random_str[] = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937_64 gen(rd());

    std::uniform_int_distribution<uint32_t> dis(0, 61);
    std::wstring randomString(length, 0);
    for (int i = 0; i < length; ++i)
        randomString[i] = random_str[dis(gen)];

    return randomString;
}

static std::string GetSystemDirectory()
{
    std::wstring tmp(4096, L'\0');
    tmp.resize(GetSystemDirectoryW(&tmp[0], static_cast<UINT>(tmp.size())));
    auto systemDirectory = System::Encoding::WCharToUtf8(tmp);

    System::String::ToLower(systemDirectory);
    return systemDirectory;
}

static std::string FindPreferedModulePath(std::string const& systemDirectory, std::string const& name)
{
    std::string res;
    std::string tmp;
    auto modules = System::GetModules();
    for (auto& item : modules)
    {
        tmp = System::String::CopyLower(item);
        if (tmp.length() >= name.length() && strcmp(tmp.c_str() + tmp.length() - name.length(), name.c_str()) == 0)
        {
            if (strncmp(tmp.c_str(), systemDirectory.c_str(), systemDirectory.length()) == 0)
                return item;

            // I don't care which one is picked if we can't find a library in the system32 folder...
            res = std::move(item);
        }
    }

    return res;
}

static std::wstring CreateDummyHWND(HWND* dummyHwnd, ATOM* dummyAtom)
{
    auto dummyWindowClassName = RandomString(64);
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    // Register a window class for creating our render window with.
    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = DefWindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst;
    windowClass.hIcon = NULL;
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = dummyWindowClassName.c_str();
    windowClass.hIconSm = NULL;

    *dummyAtom = ::RegisterClassExW(&windowClass);

    if (*dummyAtom > 0)
    {
        *dummyHwnd = ::CreateWindowExW(
            NULL,
            dummyWindowClassName.c_str(),
            L"",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            1,
            1,
            NULL,
            NULL,
            hInst,
            nullptr
        );

        assert(dummyHwnd && "Failed to create window");
    }

    return dummyWindowClassName;
}

static void DestroyDummyHWND(HWND dummyWindowHandle, const wchar_t* dummyWindowClassName)
{
    if (dummyWindowHandle != nullptr)
    {
        DestroyWindow(dummyWindowHandle);
        UnregisterClassW(dummyWindowClassName, GetModuleHandleW(nullptr));
    }
}

static void GetDXGIFunctions(IDXGISwapChain* pSwapChain, IDXGISwapChain1* pSwapChain1, IDXGISwapChain3* pSwapChain3,
    decltype(&IDXGISwapChain::Present)* pfnPresent,
    decltype(&IDXGISwapChain::ResizeBuffers)* pfnResizeBuffers,
    decltype(&IDXGISwapChain::ResizeTarget)* pfnResizeTarget,
    decltype(&IDXGISwapChain1::Present1)* pfnPresent1,
    decltype(&IDXGISwapChain3::ResizeBuffers1)* pfnResizeBuffers1)
{
    void** vTable = *reinterpret_cast<void***>(pSwapChain);
    *(void**)pfnPresent = vTable[(int)IDXGISwapChainVTable::Present];
    *(void**)pfnResizeBuffers = vTable[(int)IDXGISwapChainVTable::ResizeBuffers];
    *(void**)pfnResizeTarget = vTable[(int)IDXGISwapChainVTable::ResizeTarget];

    if (pSwapChain1 != nullptr)
    {
        vTable = *reinterpret_cast<void***>(pSwapChain1);
        *(void**)pfnPresent1 = vTable[(int)IDXGISwapChainVTable::Present1];
    }

    if (pSwapChain3 != nullptr)
    {
        vTable = *reinterpret_cast<void***>(pSwapChain3);
        *(void**)pfnResizeBuffers1 = vTable[(int)IDXGISwapChainVTable::ResizeBuffers1];
    }
}

static DirectX9Driver_t GetDX9Driver(std::string const& directX9LibraryPath, HWND windowHandle)
{
    DirectX9Driver_t driver{};
    HMODULE hD3D9 = (HMODULE)System::Library::GetLibraryHandle(directX9LibraryPath.c_str());
    if (hD3D9 == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to setup DX9", directX9LibraryPath);
        return driver;
    }

    IDirect3D9Ex* pD3D = nullptr;
    IDirect3DDevice9* pDevice = nullptr;
    IDirect3DSwapChain9* pSwapChain = nullptr;

    auto Direct3DCreate9Ex = (decltype(::Direct3DCreate9Ex)*)System::Library::GetSymbol(hD3D9, "Direct3DCreate9Ex");

    D3DPRESENT_PARAMETERS params = {};
    params.BackBufferWidth = 1;
    params.BackBufferHeight = 1;
    params.hDeviceWindow = windowHandle;
    params.BackBufferCount = 1;
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;

    if (Direct3DCreate9Ex != nullptr)
    {
        Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D);
        pD3D->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, NULL, reinterpret_cast<IDirect3DDevice9Ex**>(&pDevice));
    }

    if (pDevice == nullptr)
    {
        if (pD3D != nullptr)
        {
            pD3D->Release();
            pD3D = nullptr;
        }

        Direct3DCreate9Ex = nullptr;
        auto Direct3DCreate9 = (decltype(::Direct3DCreate9)*)System::Library::GetSymbol(hD3D9, "Direct3DCreate9");
        if (Direct3DCreate9 != nullptr)
        {
            // D3DDEVTYPE_HAL
            pD3D = reinterpret_cast<IDirect3D9Ex*>(Direct3DCreate9(D3D_SDK_VERSION));
            pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, &pDevice);
        }
    }

    if (pDevice != nullptr)
    {
        pDevice->GetSwapChain(0, &pSwapChain);

        void** vTable = *reinterpret_cast<void***>(pDevice);
        (void*&)driver.pfnRelease = vTable[(int)IDirect3DDevice9VTable::Release];
        (void*&)driver.pfnPresent = vTable[(int)IDirect3DDevice9VTable::Present];
        (void*&)driver.pfnReset = vTable[(int)IDirect3DDevice9VTable::Reset];

        if (Direct3DCreate9Ex != nullptr)
        {
            (void*&)driver.pfnPresentEx = vTable[(int)IDirect3DDevice9VTable::PresentEx];
            (void*&)driver.pfnResetEx = vTable[(int)IDirect3DDevice9VTable::ResetEx];
        }

        if (pSwapChain != nullptr)
        {
            vTable = *reinterpret_cast<void***>(pSwapChain);
            (void*&)driver.pfnSwapChainPresent = vTable[(int)IDirect3DSwapChain9VTable::Present];
        }

        driver.LibraryPath = System::Library::GetLibraryPath(hD3D9);
    }

    if (pSwapChain) pSwapChain->Release();
    if (pDevice) pDevice->Release();
    if (pD3D) pD3D->Release();

    return driver;
}

static DirectX10Driver_t GetDX10Driver(std::string const& directX10LibraryPath, std::string const& dxgiLibraryPath, HWND windowHandle)
{
    DirectX10Driver_t driver{};

    HMODULE hD3D10 = (HMODULE)System::Library::GetLibraryHandle(directX10LibraryPath.c_str());
    if (hD3D10 == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX10", directX10LibraryPath);
        return driver;
    }
    HMODULE dxgi = (HMODULE)System::Library::GetLibraryHandle(dxgiLibraryPath.c_str());
    if (dxgi == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX10", dxgiLibraryPath);
        return driver;
    }

    IDXGISwapChain1* pSwapChain1 = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;
    ID3D10Device* pDevice = nullptr;
    int version = 0;

    IDXGIFactory2* pDXGIFactory = nullptr;

    auto D3D10CreateDevice = (decltype(::D3D10CreateDevice)*)System::Library::GetSymbol(hD3D10, "D3D10CreateDevice");
    decltype(CreateDXGIFactory1)* CreateDXGIFactory1 = (decltype(CreateDXGIFactory1))System::Library::GetSymbol(dxgi, "CreateDXGIFactory1");

    if (D3D10CreateDevice != nullptr && CreateDXGIFactory1 != nullptr)
    {
        D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &pDevice);
        if (pDevice != nullptr)
        {
            CreateDXGIFactory1(IID_PPV_ARGS(&pDXGIFactory));
            if (pDXGIFactory != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
                SwapChainDesc.BufferCount = 2;
                SwapChainDesc.Width = 1;
                SwapChainDesc.Height = 1;
                SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                SwapChainDesc.Stereo = FALSE;
                SwapChainDesc.SampleDesc = { 1, 0 };
                SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                SwapChainDesc.Scaling = DXGI_SCALING_NONE;
                SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

                pDXGIFactory->CreateSwapChainForHwnd(pDevice, windowHandle, &SwapChainDesc, NULL, NULL, &pSwapChain1);
                pSwapChain = pSwapChain1;
            }
        }
    }

    if (pDXGIFactory) pDXGIFactory->Release();

    if (pDevice != nullptr && pSwapChain == nullptr)
    {
        SPDLOG_WARN("Failed to instanciate IDXGISwapChain1, fallback to pure DX10 detection");

        auto D3D10CreateDeviceAndSwapChain = (decltype(::D3D10CreateDeviceAndSwapChain)*)System::Library::GetSymbol(hD3D10, "D3D10CreateDeviceAndSwapChain");
        if (D3D10CreateDeviceAndSwapChain != nullptr)
        {
            DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};

            SwapChainDesc.BufferCount = 1;
            SwapChainDesc.BufferDesc.Width = 1;
            SwapChainDesc.BufferDesc.Height = 1;
            SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
            SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
            SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            SwapChainDesc.OutputWindow = windowHandle;
            SwapChainDesc.SampleDesc.Count = 1;
            SwapChainDesc.SampleDesc.Quality = 0;
            SwapChainDesc.Windowed = TRUE;

            D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice);
        }
    }

    if (pSwapChain != nullptr)
    {
        void** vTable = *reinterpret_cast<void***>(pDevice);
        (void*&)driver.pfnRelease = vTable[(int)ID3D10DeviceVTable::Release];

        GetDXGIFunctions(pSwapChain, pSwapChain1, nullptr,
            &driver.pfnPresent,
            &driver.pfnResizeBuffers,
            &driver.pfnResizeTarget,
            &driver.pfnPresent1,
            nullptr);

        driver.LibraryPath = System::Library::GetLibraryPath(hD3D10);
    }
    else
    {
        SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
    }
    if (pDevice)pDevice->Release();
    if (pSwapChain)pSwapChain->Release();

    return driver;
}

static DirectX11Driver_t GetDX11Driver(std::string const& directX11LibraryPath, std::string const& dxgiLibraryPath, HWND windowHandle)
{
    DirectX11Driver_t driver{};

    HMODULE hD3D11 = (HMODULE)System::Library::GetLibraryHandle(directX11LibraryPath.c_str());
    if (hD3D11 == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX11", directX11LibraryPath);
        return driver;
    }
    HMODULE dxgi = (HMODULE)System::Library::GetLibraryHandle(dxgiLibraryPath.c_str());
    if (dxgi == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX11", dxgiLibraryPath);
        return driver;
    }

    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11Device* pDevice = nullptr;
    int version = 0;

    IDXGIFactory2* pDXGIFactory = nullptr;

    auto D3D11CreateDevice = (decltype(::D3D11CreateDevice)*)System::Library::GetSymbol(hD3D11, "D3D11CreateDevice");
    decltype(CreateDXGIFactory1)* CreateDXGIFactory1 = (decltype(CreateDXGIFactory1))System::Library::GetSymbol(dxgi, "CreateDXGIFactory1");

    if (D3D11CreateDevice != nullptr && CreateDXGIFactory1 != nullptr)
    {
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &pDevice, NULL, NULL);
        if (pDevice != nullptr)
        {
            CreateDXGIFactory1(IID_PPV_ARGS(&pDXGIFactory));
            if (pDXGIFactory != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
                SwapChainDesc.BufferCount = 2;
                SwapChainDesc.Width = 1;
                SwapChainDesc.Height = 1;
                SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                SwapChainDesc.Stereo = FALSE;
                SwapChainDesc.SampleDesc = { 1, 0 };
                SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                SwapChainDesc.Scaling = DXGI_SCALING_NONE;
                SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

                pDXGIFactory->CreateSwapChainForHwnd(pDevice, windowHandle, &SwapChainDesc, NULL, NULL, reinterpret_cast<IDXGISwapChain1**>(&pSwapChain));
            }
        }
    }

    if (pDXGIFactory) pDXGIFactory->Release();

    if (pDevice != nullptr && pSwapChain != nullptr)
    {
        version = 1;
    }
    else
    {
        SPDLOG_WARN("Failed to instanciate IDXGISwapChain1, fallback to pure DX11 detection");

        auto D3D11CreateDeviceAndSwapChain = (decltype(::D3D11CreateDeviceAndSwapChain)*)System::Library::GetSymbol(hD3D11, "D3D11CreateDeviceAndSwapChain");
        if (D3D11CreateDeviceAndSwapChain != nullptr)
        {
            DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};

            SwapChainDesc.BufferCount = 1;
            SwapChainDesc.BufferDesc.Width = 1;
            SwapChainDesc.BufferDesc.Height = 1;
            SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
            SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
            SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            SwapChainDesc.OutputWindow = windowHandle;
            SwapChainDesc.SampleDesc.Count = 1;
            SwapChainDesc.SampleDesc.Quality = 0;
            SwapChainDesc.Windowed = TRUE;

            D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, NULL, D3D11_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice, NULL, NULL);
        }
    }

    if (pSwapChain != nullptr)
    {
        void** vTable = *reinterpret_cast<void***>(pDevice);
        (void*&)driver.pfnRelease = vTable[(int)ID3D11DeviceVTable::Release];

        GetDXGIFunctions(pSwapChain, version > 0 ? reinterpret_cast<IDXGISwapChain1*>(pSwapChain) : nullptr, nullptr,
            &driver.pfnPresent,
            &driver.pfnResizeBuffers,
            &driver.pfnResizeTarget,
            &driver.pfnPresent1,
            nullptr);

        driver.LibraryPath = System::Library::GetLibraryPath(hD3D11);
    }

    if (pDevice) pDevice->Release();
    if (pSwapChain) pSwapChain->Release();

    return driver;
}

static DirectX12Driver_t GetDX12Driver(std::string const& directX12LibraryPath, std::string const& dxgiLibraryPath, HWND windowHandle)
{
    DirectX12Driver_t driver{};

    HMODULE hD3D12 = (HMODULE)System::Library::GetLibraryHandle(directX12LibraryPath.c_str());
    if (hD3D12 == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX12", directX12LibraryPath);
        return driver;
    }
    HMODULE dxgi = (HMODULE)System::Library::GetLibraryHandle(dxgiLibraryPath.c_str());
    if (dxgi == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect DX12", dxgiLibraryPath);
        return driver;
    }

    IDXGIFactory4* pDXGIFactory = nullptr;
    IDXGISwapChain1* pSwapChain = nullptr;
    ID3D12CommandQueue* pCommandQueue = nullptr;
    ID3D12Device* pDevice = nullptr;

    auto D3D12CreateDevice = (decltype(::D3D12CreateDevice)*)System::Library::GetSymbol(hD3D12, "D3D12CreateDevice");
    if (D3D12CreateDevice != nullptr)
    {
        D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));

        if (pDevice != nullptr)
        {
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));

            if (pCommandQueue != nullptr)
            {
                decltype(CreateDXGIFactory1)* CreateDXGIFactory1 = (decltype(CreateDXGIFactory1))System::Library::GetSymbol(dxgi, "CreateDXGIFactory1");
                if (CreateDXGIFactory1 != nullptr)
                {
                    CreateDXGIFactory1(IID_PPV_ARGS(&pDXGIFactory));
                    if (pDXGIFactory != nullptr)
                    {
                        DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
                        SwapChainDesc.BufferCount = 2;
                        SwapChainDesc.Width = 1;
                        SwapChainDesc.Height = 1;
                        SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        SwapChainDesc.Stereo = FALSE;
                        SwapChainDesc.SampleDesc = { 1, 0 };
                        SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                        SwapChainDesc.Scaling = DXGI_SCALING_NONE;
                        SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                        SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

                        pDXGIFactory->CreateSwapChainForHwnd(pCommandQueue, windowHandle, &SwapChainDesc, NULL, NULL, &pSwapChain);
                    }
                }
            }
        }//if (pDevice != nullptr)
    }//if (D3D12CreateDevice != nullptr)

    if (pCommandQueue != nullptr && pSwapChain != nullptr)
    {
        IDXGISwapChain3* pSwapChain3;

        pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));

        {
            void** vTable = *reinterpret_cast<void***>(pDevice);
            (void*&)driver.pfnRelease = vTable[(int)ID3D12DeviceVTable::Release];
        }
        {
            void** vTable = *reinterpret_cast<void***>(pCommandQueue);
            (void*&)driver.pfnExecuteCommandLists = vTable[(int)ID3D12CommandQueueVTable::ExecuteCommandLists];
        }

        GetDXGIFunctions(pSwapChain, pSwapChain, pSwapChain3,
            &driver.pfnPresent,
            &driver.pfnResizeBuffers,
            &driver.pfnResizeTarget,
            &driver.pfnPresent1,
            &driver.pfnResizeBuffer1);

        driver.LibraryPath = System::Library::GetLibraryPath(hD3D12);

        if (pSwapChain3 != nullptr) pSwapChain3->Release();
    }

    if (pSwapChain) pSwapChain->Release();
    if (pDXGIFactory) pDXGIFactory->Release();
    if (pCommandQueue) pCommandQueue->Release();
    if (pDevice) pDevice->Release();

    return driver;
}

static OpenGLDriver_t GetOpenGLDriver(std::string const& openGLLibraryPath)
{
    OpenGLDriver_t driver{};

    HMODULE hOpenGL = (HMODULE)System::Library::GetLibraryHandle(openGLLibraryPath.c_str());
    if (hOpenGL == nullptr)
    {
        SPDLOG_WARN("Failed to load {} to detect OpenGL", openGLLibraryPath);
        return driver;
    }

    driver.wglSwapBuffers = (decltype(::SwapBuffers)*)System::Library::GetSymbol(hOpenGL, "wglSwapBuffers");
    driver.LibraryPath = System::Library::GetLibraryPath(hOpenGL);
    return driver;
}

class RendererDetector_t
{
    static RendererDetector_t* _Instance;
public:
    static RendererDetector_t* Inst()
    {
        if (_Instance == nullptr)
        {
            _Instance = new RendererDetector_t;
        }
        return _Instance;
    }

    ~RendererDetector_t()
    {
        StopDetection();

        delete _DX9Hook;
        delete _DX10Hook;
        delete _DX11Hook;
        delete _DX12Hook;
        delete _OpenGLHook;
        delete _VulkanHook;

        _Instance = nullptr;
    }

private:
    std::timed_mutex _DetectorMutex;
    std::mutex _RendererMutex;

    BaseHook_t _DetectionHooks;
    RendererHook_t* _RendererHook;

    bool _DetectionStarted;
    bool _DetectionDone;
    uint32_t _DetectionCount;
    bool _DetectionCancelled;
    std::condition_variable _StopDetectionConditionVariable;
    std::mutex _StopDetectionMutex;

    decltype(&IDXGISwapChain::Present)       _IDXGISwapChainPresent;
    decltype(&IDXGISwapChain1::Present1)     _IDXGISwapChain1Present1;
    decltype(&IDirect3DDevice9::Present)     _IDirect3DDevice9Present;
    decltype(&IDirect3DDevice9Ex::PresentEx) _IDirect3DDevice9ExPresentEx;
    decltype(&IDirect3DSwapChain9::Present)  _IDirect3DSwapChain9Present;
    decltype(::SwapBuffers)* _WGLSwapBuffers;
    decltype(::vkQueuePresentKHR)* _VkQueuePresentKHR;

    bool _DXGIHooked;
    bool _DXGI1_2Hooked;
    bool _DX12Hooked;
    bool _DX11Hooked;
    bool _DX10Hooked;
    bool _DX9Hooked;
    bool _OpenGLHooked;
    bool _VulkanHooked;

    DX12Hook_t* _DX12Hook;
    DX11Hook_t* _DX11Hook;
    DX10Hook_t* _DX10Hook;
    DX9Hook_t* _DX9Hook;
    OpenGLHook_t* _OpenGLHook;
    VulkanHook_t* _VulkanHook;

    std::string _SystemDirectory;
    HWND _DummyWindowHandle;
    std::wstring _DummyWindowClassName;
    ATOM _DummyWindowAtom;

    RendererDetector_t() :
        _RendererHook(false),
        _DetectionStarted(false),
        _DetectionDone(false),
        _DetectionCount(0),
        _DetectionCancelled(false),
        _IDXGISwapChainPresent(nullptr),
        _IDXGISwapChain1Present1(nullptr),
        _IDirect3DDevice9Present(nullptr),
        _IDirect3DDevice9ExPresentEx(nullptr),
        _IDirect3DSwapChain9Present(nullptr),
        _WGLSwapBuffers(nullptr),
        _DXGIHooked(false),
        _DXGI1_2Hooked(false),
        _DX12Hooked(false),
        _DX11Hooked(false),
        _DX10Hooked(false),
        _DX9Hooked(false),
        _OpenGLHooked(false),
        _VulkanHooked(false),
        _DX9Hook(nullptr),
        _DX10Hook(nullptr),
        _DX11Hook(nullptr),
        _DX12Hook(nullptr),
        _OpenGLHook(nullptr),
        _VulkanHook(nullptr),
        _SystemDirectory(GetSystemDirectory()),
        _DummyWindowHandle(nullptr),
        _DummyWindowClassName(RandomString(64)),
        _DummyWindowAtom(0)
    {
    }

    template<typename T>
    void _HookDetected(T*& detected_renderer)
    {
        _DetectionHooks.UnhookAll();
        _RendererHook = static_cast<InGameOverlay::RendererHook_t*>(detected_renderer);
        detected_renderer = nullptr;
        _DetectionDone = true;
        DestroyDummyHWND(_DummyWindowHandle, _DummyWindowClassName.c_str());
    }

    void _DeduceDXVersionFromSwapChain(IDXGISwapChain* pSwapChain)
    {
        IUnknown* pDevice = nullptr;
        if (Inst()->_DX12Hooked)
        {
            pSwapChain->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D12Device**>(&pDevice)));
        }
        if (pDevice != nullptr)
        {
            _HookDetected(_DX12Hook);
        }
        else
        {
            if (_DX11Hooked)
            {
                pSwapChain->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D11Device**>(&pDevice)));
                if (pDevice != nullptr)
                {
                    // It seems that when you are using a DX10 device, sometimes, the swapchain has a DX11 device.
                    ID3D10Device* pD10Device = nullptr;
                    pSwapChain->GetDevice(IID_PPV_ARGS(&pD10Device));
                    if (pD10Device != nullptr)
                    {
                        pDevice->Release();
                        pD10Device->Release();
                        pDevice = nullptr;
                    }
                }
            }
            if (pDevice != nullptr)
            {
                _HookDetected(_DX11Hook);
            }
            else
            {
                if (_DX10Hooked)
                {
                    pSwapChain->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D10Device**>(&pDevice)));
                }
                if (pDevice != nullptr)
                {
                    _HookDetected(_DX10Hook);
                }
            }
        }
        if (pDevice != nullptr)
        {
            pDevice->Release();
        }
    }

    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags)
    {
        auto inst = Inst();
        HRESULT res;
        // It appears that (NVidia at least) calls IDXGISwapChain when calling OpenGL or Vulkan SwapBuffers.
        // So only lock when OpenGL or Vulkan hasn't already locked the mutex.
        std::unique_lock<std::mutex> lk(inst->_RendererMutex, std::try_to_lock);

        SPDLOG_INFO("IDXGISwapChain::Present");
        res = (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        inst->_DeduceDXVersionFromSwapChain(_this);

        return res;
    }

    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainPresent1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
    {
        auto inst = Inst();
        HRESULT res;
        // It appears that (NVidia at least) calls IDXGISwapChain when calling OpenGL or Vulkan SwapBuffers.
        // So only lock when OpenGL or Vulkan hasn't already locked the mutex.
        std::unique_lock<std::mutex> lk(inst->_RendererMutex, std::try_to_lock);

        SPDLOG_INFO("IDXGISwapChain::Present1");
        res = (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        inst->_DeduceDXVersionFromSwapChain(_this);

        return res;
    }

    static HRESULT STDMETHODCALLTYPE _MyDX9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        SPDLOG_INFO("IDirect3DDevice9::Present");
        auto res = (_this->*inst->_IDirect3DDevice9Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        inst->_HookDetected(inst->_DX9Hook);

        return res;
    }

    static HRESULT STDMETHODCALLTYPE _MyDX9PresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        SPDLOG_INFO("IDirect3DDevice9Ex::PresentEx");
        auto res = (_this->*inst->_IDirect3DDevice9ExPresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        inst->_HookDetected(inst->_DX9Hook);

        return res;
    }

    static HRESULT STDMETHODCALLTYPE _MyDX9SwapChainPresent(IDirect3DSwapChain9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        SPDLOG_INFO("IDirect3DSwapChain9::Present");
        auto res = (_this->*inst->_IDirect3DSwapChain9Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        inst->_HookDetected(inst->_DX9Hook);

        return res;
    }

    static BOOL WINAPI _MyWGLSwapBuffers(HDC hDC)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        SPDLOG_INFO("wglSwapBuffers");
        auto res = inst->_WGLSwapBuffers(hDC);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return res;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
        {
            inst->_HookDetected(inst->_OpenGLHook);
        }

        return res;
    }

    static VkResult VKAPI_CALL _MyVkQueuePresentKHR(VkQueue Queue, const VkPresentInfoKHR* pPresentInfo)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        auto res = inst->_VkQueuePresentKHR(Queue, pPresentInfo);
        if (inst->_DetectionDone)
            return res;

        inst->_HookDetected(inst->_VulkanHook);

        return res;
    }

    void _HookDXGI(decltype(&IDXGISwapChain::Present) pfnPresent, decltype(&IDXGISwapChain1::Present1) pfnPresent1)
    {
        if (!_DXGIHooked && pfnPresent != nullptr)
        {
            _DXGIHooked = true;

            _IDXGISwapChainPresent = pfnPresent;
            _DetectionHooks.BeginHook();
            TRY_HOOK_FUNCTION(_IDXGISwapChainPresent, &RendererDetector_t::_MyIDXGISwapChainPresent);
            _DetectionHooks.EndHook();
        }
        if (!_DXGI1_2Hooked && pfnPresent1 != nullptr)
        {
            _DXGI1_2Hooked = true;

            _IDXGISwapChain1Present1 = pfnPresent1;
            _DetectionHooks.BeginHook();
            TRY_HOOK_FUNCTION(_IDXGISwapChain1Present1, &RendererDetector_t::_MyIDXGISwapChainPresent1);
            _DetectionHooks.EndHook();
        }
    }

    void _HookDX9Present(
        decltype(&IDirect3DDevice9::Present) pPresent,
        decltype(&IDirect3DDevice9Ex::PresentEx) pPresentEx,
        decltype(&IDirect3DSwapChain9::Present) pSwapChainPresent)
    {
        _IDirect3DDevice9Present = pPresent;

        _DetectionHooks.BeginHook();
        TRY_HOOK_FUNCTION(_IDirect3DDevice9Present, &RendererDetector_t::_MyDX9Present);
        _DetectionHooks.EndHook();

        if (pPresentEx != nullptr)
        {
            _IDirect3DDevice9ExPresentEx = pPresentEx;

            _DetectionHooks.BeginHook();
            TRY_HOOK_FUNCTION(_IDirect3DDevice9ExPresentEx, &RendererDetector_t::_MyDX9PresentEx);
            _DetectionHooks.EndHook();
        }

        if (pSwapChainPresent != nullptr)
        {
            _IDirect3DSwapChain9Present = pSwapChainPresent;

            _DetectionHooks.BeginHook();
            TRY_HOOK_FUNCTION(_IDirect3DSwapChain9Present, &RendererDetector_t::_MyDX9SwapChainPresent);
            _DetectionHooks.EndHook();
        }
    }

    void _HookVkQueuePresentKHR(decltype(::vkQueuePresentKHR)* _vkQueuePresentKHR)
    {
        _VkQueuePresentKHR = _vkQueuePresentKHR;

        _DetectionHooks.BeginHook();
        _DetectionHooks.HookFuncs(
            std::pair<void**, void*>{ (void**)&_VkQueuePresentKHR, (void*)&_MyVkQueuePresentKHR }
        );
        _DetectionHooks.EndHook();
    }

    void _HookDX9(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_DX9Hooked)
        {
            auto driver = GetDX9Driver(libraryPath, _DummyWindowHandle);
            if (driver.pfnPresent != nullptr)
            {
                SPDLOG_INFO("Hooked D3D9::Present to detect DX Version");
                _DX9Hooked = true;

                _HookDX9Present(driver.pfnPresent, driver.pfnPresentEx, driver.pfnSwapChainPresent);

                _DX9Hook = DX9Hook_t::Inst();
                _DX9Hook->LibraryName = driver.LibraryPath;
                _DX9Hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnReset, driver.pfnPresentEx, driver.pfnResetEx, driver.pfnSwapChainPresent);
            }
            else
            {
                SPDLOG_WARN("Failed to hook D3D9::Present to detect DX Version");
            }
        }
    }

    void _HookDX10(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_DX10Hooked)
        {
            auto driver = GetDX10Driver(libraryPath, preferSystemLibraries ? FindPreferedModulePath(_SystemDirectory, DXGI_DLL_NAME) : DXGI_DLL_NAME, _DummyWindowHandle);
            if (driver.pfnPresent != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");
                _DX10Hooked = true;

                _HookDXGI(driver.pfnPresent, driver.pfnPresent1);

                _DX10Hook = DX10Hook_t::Inst();
                _DX10Hook->LibraryName = driver.LibraryPath;
                _DX10Hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }
        }
    }

    void _HookDX11(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_DX11Hooked)
        {
            auto driver = GetDX11Driver(libraryPath, preferSystemLibraries ? FindPreferedModulePath(_SystemDirectory, DXGI_DLL_NAME) : DXGI_DLL_NAME, _DummyWindowHandle);
            if (driver.pfnPresent != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");
                _DX11Hooked = true;

                _HookDXGI(driver.pfnPresent, driver.pfnPresent1);

                _DX11Hook = DX11Hook_t::Inst();
                _DX11Hook->LibraryName = driver.LibraryPath;
                _DX11Hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }
        }
    }

    void _HookDX12(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_DX12Hooked)
        {
            auto driver = GetDX12Driver(libraryPath, preferSystemLibraries ? FindPreferedModulePath(_SystemDirectory, DXGI_DLL_NAME) : DXGI_DLL_NAME, _DummyWindowHandle);
            if (driver.pfnPresent != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");
                _DX12Hooked = true;

                _HookDXGI(driver.pfnPresent, driver.pfnPresent1);

                _DX12Hook = DX12Hook_t::Inst();
                _DX12Hook->LibraryName = driver.LibraryPath;
                _DX12Hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1, driver.pfnResizeBuffer1, driver.pfnExecuteCommandLists);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }
        }
    }

    void _HookOpenGL(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_OpenGLHooked)
        {
            auto driver = GetOpenGLDriver(libraryPath);
            if (driver.wglSwapBuffers != nullptr)
            {
                SPDLOG_INFO("Hooked wglSwapBuffers to detect OpenGL");
                _OpenGLHooked = true;

                _WGLSwapBuffers = driver.wglSwapBuffers;

                _OpenGLHook = OpenGLHook_t::Inst();
                _OpenGLHook->LibraryName = driver.LibraryPath;
                _OpenGLHook->LoadFunctions(_WGLSwapBuffers);

                _DetectionHooks.BeginHook();
                TRY_HOOK_FUNCTION(_WGLSwapBuffers, &RendererDetector_t::_MyWGLSwapBuffers);
                _DetectionHooks.EndHook();
            }
            else
            {
                SPDLOG_WARN("Failed to Hook wglSwapBuffers to detect OpenGL");
            }
        }
    }

    void _HookVulkan(std::string const& libraryPath, bool preferSystemLibraries)
    {
        // Vulkan hook disabled until proper implementation.
        return;
        if (!_VulkanHooked)
        {
            System::Library::Library libVulkan;
            if (!libVulkan.OpenLibrary(libraryPath, false))
            {
                SPDLOG_WARN("Failed to load {} to detect Vulkan", libraryPath);
                return;
            }

            auto vkCreateInstance = libVulkan.GetSymbol<decltype(::vkCreateInstance)>("vkCreateInstance");
            auto vkDestroyInstance = libVulkan.GetSymbol<decltype(::vkDestroyInstance)>("vkDestroyInstance");
            auto vkGetInstanceProcAddr = libVulkan.GetSymbol<decltype(::vkGetInstanceProcAddr)>("vkGetInstanceProcAddr");

            decltype(::vkQueuePresentKHR)* vkQueuePresentKHR = nullptr;
            decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR = nullptr;
            decltype(::vkAcquireNextImage2KHR)* vkAcquireNextImage2KHR = nullptr;

            VkInstanceCreateInfo instance_infos{};
            VkInstance instance{};
            std::vector<VkPhysicalDevice> phyDevices;
            VkDeviceCreateInfo create_info{};
            VkDevice pDevice{};
            uint32_t count = 0;

            instance_infos.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            vkCreateInstance(&instance_infos, nullptr, &instance);

            auto vkCreateDevice = (decltype(::vkCreateDevice)*)vkGetInstanceProcAddr(instance, "vkCreateDevice");
            auto vkDestroyDevice = (decltype(::vkDestroyDevice)*)vkGetInstanceProcAddr(instance, "vkDestroyDevice");
            auto vkGetDeviceProcAddr = (decltype(::vkGetDeviceProcAddr)*)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
            auto vkEnumeratePhysicalDevices = (decltype(::vkEnumeratePhysicalDevices)*)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
            auto vkEnumerateDeviceExtensionProperties = (decltype(::vkEnumerateDeviceExtensionProperties)*)vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties");
            auto vkGetPhysicalDeviceProperties = (decltype(::vkGetPhysicalDeviceProperties)*)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");

            vkEnumeratePhysicalDevices(instance, &count, nullptr);
            phyDevices.resize(count);
            vkEnumeratePhysicalDevices(instance, &count, phyDevices.data());

            [&]()
            {// Lambda for nested for break.
                VkPhysicalDeviceProperties props{};
                std::vector<VkExtensionProperties> ext_props;

                for (auto& device : phyDevices)
                {
                    vkGetPhysicalDeviceProperties(device, &props);
                    if (props.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || props.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                    {
                        count = 0;
                        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
                        ext_props.resize(count);
                        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, ext_props.data());

                        for (auto& ext : ext_props)
                        {
                            if (strcmp(ext.extensionName, "Vk_KHR_swapchain") == 0)
                            {
                                create_info.sType = VkStructureType::VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                                create_info.enabledExtensionCount = 1;
                                const char* str = "Vk_KHR_swapchain";
                                create_info.ppEnabledExtensionNames = &str;
                                vkCreateDevice(device, &create_info, nullptr, &pDevice);
                                if (pDevice != nullptr)
                                    return;
                            }
                        }
                    }
                }
            }();

            if (pDevice != nullptr)
            {
                vkQueuePresentKHR = (decltype(::vkQueuePresentKHR)*)vkGetDeviceProcAddr(pDevice, "vkQueuePresentKHR");
                vkAcquireNextImageKHR = (decltype(::vkAcquireNextImageKHR)*)vkGetDeviceProcAddr(pDevice, "vkAcquireNextImageKHR");
                vkAcquireNextImage2KHR = (decltype(::vkAcquireNextImage2KHR)*)vkGetDeviceProcAddr(pDevice, "vkAcquireNextImage2KHR");
                vkDestroyDevice(pDevice, nullptr);
            }
            vkDestroyInstance(instance, nullptr);

            if (vkQueuePresentKHR != nullptr /* && (vkAcquireNextImageKHR != nullptr || vkAcquireNextImage2KHR != nullptr)*/)
            {
                SPDLOG_INFO("Hooked vkQueuePresentKHR to detect Vulkan");

                _VulkanHooked = true;

                _VulkanHook = VulkanHook_t::Inst();
                _VulkanHook->LibraryName = libraryPath;
                _VulkanHook->LoadFunctions(vkQueuePresentKHR);

                _HookVkQueuePresentKHR(vkQueuePresentKHR);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook vkQueuePresentKHR to detect Vulkan");
            }
        }
    }

    bool _EnterDetection()
    {
        _DummyWindowClassName = CreateDummyHWND(&_DummyWindowHandle, &_DummyWindowAtom);
        return _DummyWindowHandle != nullptr;
    }

    void _ExitDetection()
    {
        DestroyDummyHWND(_DummyWindowHandle, _DummyWindowClassName.c_str());

        _DetectionDone = true;
        _DetectionHooks.UnhookAll();

        _DXGIHooked    = false;
        _DXGI1_2Hooked = false;
        _DX12Hooked    = false;
        _DX11Hooked    = false;
        _DX10Hooked    = false;
        _DX9Hooked     = false;
        _OpenGLHooked  = false;
        _VulkanHooked  = false;

        delete _DX9Hook   ; _DX9Hook    = nullptr;
        delete _DX10Hook  ; _DX10Hook   = nullptr;
        delete _DX11Hook  ; _DX11Hook   = nullptr;
        delete _DX12Hook  ; _DX12Hook   = nullptr;
        delete _OpenGLHook; _OpenGLHook = nullptr;
        delete _VulkanHook; _VulkanHook = nullptr;
    }

public:
    std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout, bool preferSystemLibraries)
    {
        std::lock_guard<std::mutex> lk(_StopDetectionMutex);

        if (_DetectionCount == 0)
        {// If we have no detections in progress, restart detection.
            _DetectionCancelled = false;
        }

        ++_DetectionCount;

        return std::async(std::launch::async, [this, timeout, preferSystemLibraries]() -> InGameOverlay::RendererHook_t*
        {
            std::unique_lock<std::timed_mutex> detection_lock(_DetectorMutex, std::defer_lock);
            constexpr std::chrono::milliseconds infiniteTimeout{ -1 };
        
            if (!detection_lock.try_lock_for(timeout))
            {
                --_DetectionCount;
                return nullptr;
            }

            bool cancel = false;
            {
                auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);

                if (!_DetectionCancelled)
                {
                    if (_DetectionDone)
                    {
                        if (_RendererHook == nullptr)
                        {// Renderer detection was run but we didn't find it, restart the detection
                            _DetectionDone = false;
                        }
                        else
                        {// Renderer already detected, cancel detection and return the renderer.
                            cancel = true;
                        }
                    }

                    if (!_EnterDetection())
                        cancel = true;
                }
                else
                {// Detection was cancelled, cancel this detection
                    cancel = true;
                }
            }

            if (cancel)
            {
                --_DetectionCount;
                _StopDetectionConditionVariable.notify_all();
                return _RendererHook;
            }

            SPDLOG_TRACE("Started renderer detection.");

            std::pair<std::string, void(RendererDetector_t::*)(std::string const&, bool)> libraries[]
            {
                { OPENGL_DLL_NAME, &RendererDetector_t::_HookOpenGL },
                { VULKAN_DLL_NAME, &RendererDetector_t::_HookVulkan },
                {   DX12_DLL_NAME, &RendererDetector_t::_HookDX12   },
                {   DX11_DLL_NAME, &RendererDetector_t::_HookDX11   },
                {   DX10_DLL_NAME, &RendererDetector_t::_HookDX10   },
                {    DX9_DLL_NAME, &RendererDetector_t::_HookDX9    },
            };
            std::string name;

            auto startTime = std::chrono::steady_clock::now();
            do
            {
                std::unique_lock<std::mutex> lck(_StopDetectionMutex);
                if (_DetectionCancelled || _DetectionDone)
                    break;

                for (auto const& library : libraries)
                {
                    std::string libraryPath = preferSystemLibraries ? FindPreferedModulePath(_SystemDirectory, library.first) : library.first;
                    if (!libraryPath.empty())
                    {
                        void* libraryHandle = System::Library::GetLibraryHandle(libraryPath.c_str());
                        if (libraryHandle != nullptr)
                        {
                            std::lock_guard<std::mutex> lk(_RendererMutex);
                            (this->*library.second)(System::Library::GetLibraryPath(libraryHandle), preferSystemLibraries);
                        }
                    }
                }

                _StopDetectionConditionVariable.wait_for(lck, std::chrono::milliseconds{ 100 });
                if (!_DetectionStarted)
                {
                    std::lock_guard<std::mutex> lck(_RendererMutex);
                    _DetectionStarted = true;
                }
            } while (timeout == infiniteTimeout || (std::chrono::steady_clock::now() - startTime) <= timeout);

            _DetectionStarted = false;
            {
                auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);
                
                _ExitDetection();

                --_DetectionCount;
            }
            _StopDetectionConditionVariable.notify_all();

            SPDLOG_TRACE("Renderer detection done {}.", (void*)_RendererHook);

            return _RendererHook;
        });
    }

    void StopDetection()
    {
        {
            std::lock_guard<std::mutex> lk(_StopDetectionMutex);
            if (_DetectionCount == 0)
                return;
        }
        {
            auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);
            _DetectionCancelled = true;
        }
        _StopDetectionConditionVariable.notify_all();
        {
            std::unique_lock<std::mutex> lk(_StopDetectionMutex);
            _StopDetectionConditionVariable.wait(lk, [&]() { return _DetectionCount == 0; });
        }
    }
};

RendererDetector_t* RendererDetector_t::_Instance = nullptr;

#ifdef INGAMEOVERLAY_USE_SPDLOG

static inline void SetupSpdLog()
{   
    static std::once_flag once;
    std::call_once(once, []()
    {
        auto sinks = std::make_shared<spdlog::sinks::dist_sink_mt>();

#if defined(SYSTEM_OS_WINDOWS) && defined(_DEBUG)
        sinks->add_sink(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

        sinks->add_sink(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

        auto logger = std::make_shared<spdlog::logger>("RendererDetectorDebugLogger", sinks);

        spdlog::register_logger(logger);

        logger->set_pattern("[%H:%M:%S.%e](%t)[%l] - %!{%#} - %v");
        spdlog::set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    });
}

#endif

std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout, bool preferSystemLibraries)
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    return RendererDetector_t::Inst()->DetectRenderer(timeout, preferSystemLibraries);
}

void StopRendererDetection()
{
    RendererDetector_t::Inst()->StopDetection();
}

void FreeDetector()
{
    delete RendererDetector_t::Inst();
}

RendererHook_t* CreateRendererHook(RendererHookType_t hookType, bool preferSystemLibraries)
{
    RendererHook_t* rendererHook = nullptr;
    std::string systemDirectory = preferSystemLibraries ? GetSystemDirectory() : std::string();

#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif

    switch (hookType)
    {
        case RendererHookType_t::DirectX9:
        {
            HWND dummyWindow;
            ATOM dummyAtom;
            auto dummyClassName = CreateDummyHWND(&dummyWindow, &dummyAtom);
            if (dummyWindow != nullptr)
            {
                auto driver = GetDX9Driver(DX9_DLL_NAME, dummyWindow);
                if (driver.pfnPresent != nullptr)
                {
                    auto hook = DX9Hook_t::Inst();
                    hook->LibraryName = driver.LibraryPath;
                    hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnReset, driver.pfnPresentEx, driver.pfnResetEx, driver.pfnSwapChainPresent);
                    rendererHook = hook;
                }
                DestroyDummyHWND(dummyWindow, dummyClassName.c_str());
            }
        }
        break;

        case RendererHookType_t::DirectX10:
        {
            HWND dummyWindow;
            ATOM dummyAtom;
            auto dummyClassName = CreateDummyHWND(&dummyWindow, &dummyAtom);
            if (dummyWindow != nullptr)
            {
                auto driver = GetDX10Driver(DX10_DLL_NAME, DXGI_DLL_NAME, dummyWindow);
                if (driver.pfnPresent != nullptr)
                {
                    auto hook = DX10Hook_t::Inst();
                    hook->LibraryName = driver.LibraryPath;
                    hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1);
                    rendererHook = hook;
                }
                DestroyDummyHWND(dummyWindow, dummyClassName.c_str());
            }
        }
        break;

        case RendererHookType_t::DirectX11:
        {
            HWND dummyWindow;
            ATOM dummyAtom;
            auto dummyClassName = CreateDummyHWND(&dummyWindow, &dummyAtom);
            if (dummyWindow != nullptr)
            {
                auto driver = GetDX11Driver(DX11_DLL_NAME, DXGI_DLL_NAME, dummyWindow);
                if (driver.pfnPresent != nullptr)
                {
                    auto hook = DX11Hook_t::Inst();
                    hook->LibraryName = driver.LibraryPath;
                    hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1);
                    rendererHook = hook;
                }
                DestroyDummyHWND(dummyWindow, dummyClassName.c_str());
            }
        }
        break;

        case RendererHookType_t::DirectX12:
        {
            HWND dummyWindow;
            ATOM dummyAtom;
            auto dummyClassName = CreateDummyHWND(&dummyWindow, &dummyAtom);
            if (dummyWindow != nullptr)
            {
                auto driver = GetDX12Driver(DX12_DLL_NAME, DXGI_DLL_NAME, dummyWindow);
                if (driver.pfnPresent != nullptr)
                {
                    auto hook = DX12Hook_t::Inst();
                    hook->LibraryName = driver.LibraryPath;
                    hook->LoadFunctions(driver.pfnRelease, driver.pfnPresent, driver.pfnResizeBuffers, driver.pfnResizeTarget, driver.pfnPresent1, driver.pfnResizeBuffer1, driver.pfnExecuteCommandLists);
                    rendererHook = hook;
                }
                DestroyDummyHWND(dummyWindow, dummyClassName.c_str());
            }
        }
        break;

        case RendererHookType_t::OpenGL:
        {
            auto driver = GetOpenGLDriver(OPENGL_DLL_NAME);
            if (driver.wglSwapBuffers != nullptr)
            {
                auto hook = OpenGLHook_t::Inst();
                hook->LibraryName = driver.LibraryPath;
                hook->LoadFunctions(driver.wglSwapBuffers);
                rendererHook = hook;
            }
        }
        break;

        case RendererHookType_t::Vulkan:
        {
        }
        break;
    }

    return rendererHook;
}

}// namespace InGameOverlay