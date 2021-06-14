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

#include "Renderer_Detector.h"

#include <library/library.h>
#include <mini_detour.h>

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__) \
 || defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
    #define RENDERERDETECTOR_OS_WINDOWS
#elif defined(__linux__) || defined(linux)
    #define RENDERERDETECTOR_OS_LINUX
#elif defined(__APPLE__)
    #define RENDERERDETECTOR_OS_APPLE
#endif

#ifdef RENDERERDETECTOR_OS_WINDOWS
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "windows/DirectX_VTables.h"
#include "windows/DX12_Hook.h"
#include "windows/DX11_Hook.h"
#include "windows/DX10_Hook.h"
#include "windows/DX9_Hook.h"
#include "windows/OpenGL_Hook.h"
#include "windows/Vulkan_Hook.h"

static constexpr auto windowClassName = L"___overlay_window_class___";
class Renderer_Detector
{
    static Renderer_Detector* instance;
public:
    static Renderer_Detector* Inst()
    {
        if (instance == nullptr)
        {
            instance = new Renderer_Detector;
        }
        return instance;
    }

    ~Renderer_Detector()
    {
        delete dx9_hook;
        delete dx10_hook;
        delete dx11_hook;
        delete dx12_hook;
        delete opengl_hook;
        delete vulkan_hook;
    }

private:
    Renderer_Detector():
        dxgi_hooked(false),
        dx12_hooked(false),
        dx11_hooked(false),
        dx10_hooked(false),
        dx9_hooked(false),
        opengl_hooked(false),
        vulkan_hooked(false),
        renderer_hook(nullptr),
        dx9_hook(nullptr),
        dx10_hook(nullptr),
        dx11_hook(nullptr),
        dx12_hook(nullptr),
        opengl_hook(nullptr),
        vulkan_hook(nullptr),
        vulkan_ogl_swap(false),
        detection_done(false)
    {}

    std::mutex renderer_mutex;

    Base_Hook hooks;

    decltype(&IDXGISwapChain::Present)       IDXGISwapChainPresent;
    decltype(&IDirect3DDevice9::Present)     IDirect3DDevice9Present;
    decltype(&IDirect3DDevice9Ex::PresentEx) IDirect3DDevice9ExPresentEx;
    decltype(::SwapBuffers)*                 wglSwapBuffers;
    decltype(::vkQueuePresentKHR)*           vkQueuePresentKHR;

    bool dxgi_hooked;
    bool dx12_hooked;
    bool dx11_hooked;
    bool dx10_hooked;
    bool dx9_hooked;
    bool opengl_hooked;
    bool vulkan_hooked;

    Renderer_Hook* renderer_hook;
    DX12_Hook*   dx12_hook;
    DX11_Hook*   dx11_hook;
    DX10_Hook*   dx10_hook;
    DX9_Hook*    dx9_hook;
    OpenGL_Hook* opengl_hook;
    Vulkan_Hook* vulkan_hook;
    
    bool vulkan_ogl_swap;
    bool detection_done;

    HWND dummyWindow = nullptr;
    ATOM atom = 0;

    HWND create_hwnd()
    {
        if (dummyWindow == nullptr)
        {
            HINSTANCE hInst = GetModuleHandle(nullptr);
            if (atom == 0)
            {
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
                windowClass.lpszClassName = windowClassName;
                windowClass.hIconSm = NULL;

                atom = ::RegisterClassExW(&windowClass);
            }

            if (atom > 0)
            {
                dummyWindow = ::CreateWindowExW(
                    NULL,
                    windowClassName,
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

                assert(dummyWindow && "Failed to create window");
            }
        }

        return dummyWindow;
    }

    void destroy_hwnd()
    {
        if (dummyWindow != nullptr)
        {
            DestroyWindow(dummyWindow);
            UnregisterClassW(windowClassName, GetModuleHandle(nullptr));

            dummyWindow = nullptr;
            atom = 0;
        }
    }

    static HRESULT STDMETHODCALLTYPE MyIDXGISwapChain_Present(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags)
    {
        auto inst = Inst();
        std::unique_ptr<std::lock_guard<std::mutex>> lk;
        if (!inst->vulkan_ogl_swap)
        {// It appears that (NVidia at least) calls IDXGISwapChain when calling OpenGL or Vulkan SwapBuffers.
         // So only lock when OpenGL or Vulkan hasn't already locked the mutex.
            lk = std::make_unique<std::lock_guard<std::mutex>>(inst->renderer_mutex);
        }

        auto res = (_this->*inst->IDXGISwapChainPresent)(SyncInterval, Flags);
        if (inst->detection_done || inst->vulkan_ogl_swap)
            return res;

        IUnknown* pDevice = nullptr;
        if (Inst()->dx12_hooked)
        {
            _this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D12Device**>(&pDevice)));
        }
        if (pDevice != nullptr)
        {
            inst->hooks.UnhookAll();
            inst->renderer_hook = static_cast<Renderer_Hook*>(inst->dx12_hook);
            inst->dx12_hook = nullptr;
            inst->detection_done = true;
        }
        else
        {
            if (inst->dx11_hooked)
            {
                _this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D11Device**>(&pDevice)));
            }
            if (pDevice != nullptr)
            {
                inst->hooks.UnhookAll();
                inst->renderer_hook = static_cast<Renderer_Hook*>(inst->dx11_hook);
                inst->dx11_hook = nullptr;
                inst->detection_done = true;
            }
            else
            {
                if (inst->dx10_hooked)
                {
                    _this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D10Device**>(&pDevice)));
                }
                if (pDevice != nullptr)
                {
                    inst->hooks.UnhookAll();
                    inst->renderer_hook = static_cast<Renderer_Hook*>(inst->dx10_hook);
                    inst->dx10_hook = nullptr;
                    inst->detection_done = true;
                }
            }
        }
        if (pDevice != nullptr)
        {
            pDevice->Release();
        }

        return res;
    }

    static HRESULT STDMETHODCALLTYPE MyDX9Present(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);

        auto res = (_this->*inst->IDirect3DDevice9Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
        if (inst->detection_done)
            return res;

        inst->hooks.UnhookAll();
        inst->renderer_hook = static_cast<Renderer_Hook*>(inst->dx9_hook);
        inst->dx9_hook = nullptr;
        inst->detection_done = true;

        return res;
    }

    static HRESULT STDMETHODCALLTYPE MyDX9PresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);

        auto res = (_this->*inst->IDirect3DDevice9ExPresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        if (inst->detection_done)
            return res;

        inst->hooks.UnhookAll();
        inst->renderer_hook = static_cast<Renderer_Hook*>(inst->dx9_hook);
        inst->dx9_hook = nullptr;
        inst->detection_done = true;

        return res;
    }

    static BOOL WINAPI MywglSwapBuffers(HDC hDC)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->vulkan_ogl_swap = true;

        auto res = inst->wglSwapBuffers(hDC);
        if (inst->detection_done)
            return res;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
        {
            inst->hooks.UnhookAll();
            inst->renderer_hook = static_cast<Renderer_Hook*>(inst->opengl_hook);
            inst->opengl_hook = nullptr;
            inst->detection_done = true;
        }

        inst->vulkan_ogl_swap = false;
        return res;
    }

    static VkResult VKAPI_CALL MyvkQueuePresentKHR(VkQueue Queue, const VkPresentInfoKHR* pPresentInfo)
    {
        auto inst = Inst();
        auto res = inst->vkQueuePresentKHR(Queue, pPresentInfo);
        inst->hooks.UnhookAll();
        inst->renderer_hook = static_cast<Renderer_Hook*>(inst->vulkan_hook);
        inst->vulkan_hook = nullptr;
        inst->detection_done = true;

        return res;
    }

    void HookDXGIPresent(IDXGISwapChain* pSwapChain, decltype(&IDXGISwapChain::Present)& pfnPresent, decltype(&IDXGISwapChain::ResizeBuffers)& pfnResizeBuffers, decltype(&IDXGISwapChain::ResizeTarget)& pfnResizeTarget)
    {
        void** vTable = *reinterpret_cast<void***>(pSwapChain);
        (void*&)pfnPresent = vTable[(int)IDXGISwapChainVTable::Present];
        (void*&)pfnResizeBuffers = vTable[(int)IDXGISwapChainVTable::ResizeBuffers];
        (void*&)pfnResizeTarget = vTable[(int)IDXGISwapChainVTable::ResizeTarget];

        if (!dxgi_hooked)
        {
            dxgi_hooked = true;

            (void*&)IDXGISwapChainPresent = vTable[(int)IDXGISwapChainVTable::Present];
            hooks.BeginHook();
            hooks.HookFunc(std::pair<void**, void*>{ (void**)&IDXGISwapChainPresent, (void*)&MyIDXGISwapChain_Present});
            hooks.EndHook();
        }
    }

    void HookDX9Present(IDirect3DDevice9* pDevice, bool ex)
    {
        void** vTable = *reinterpret_cast<void***>(pDevice);
        (void*&)IDirect3DDevice9Present = vTable[(int)IDirect3DDevice9VTable::Present];

        hooks.BeginHook();
        hooks.HookFunc(std::pair<void**, void*>{ (void**)&IDirect3DDevice9Present, (void*)&MyDX9Present });
        hooks.EndHook();

        if (ex)
        {
            (void*&)IDirect3DDevice9ExPresentEx = vTable[(int)IDirect3DDevice9VTable::PresentEx];

            hooks.BeginHook();
            hooks.HookFunc(std::pair<void**, void*>{ (void**)&IDirect3DDevice9ExPresentEx, (void*)&MyDX9PresentEx });
            hooks.EndHook();
        }
        else
        {
            IDirect3DDevice9Present = nullptr;
        }
    }

    void HookwglSwapBuffers(decltype(::SwapBuffers)* _wglSwapBuffers)
    {
        wglSwapBuffers = _wglSwapBuffers;

        hooks.BeginHook();
        hooks.HookFunc(std::pair<void**, void*>{ (void**)&wglSwapBuffers, (void*)&MywglSwapBuffers });
        hooks.EndHook();
    }

    void HookvkQueuePresentKHR(decltype(::vkQueuePresentKHR)* _vkQueuePresentKHR)
    {
        vkQueuePresentKHR = _vkQueuePresentKHR;

        hooks.BeginHook();
        hooks.HookFuncs(
            std::pair<void**, void*>{ (void**)&vkQueuePresentKHR, (void*)&MyvkQueuePresentKHR }
        );
        hooks.EndHook();
    }

    void hook_dx9()
    {
        if (!dx9_hooked)
        {
            IDirect3D9Ex* pD3D = nullptr;
            IUnknown* pDevice = nullptr;
            HMODULE library = LoadLibraryA(DX9_Hook::DLL_NAME);
            decltype(Direct3DCreate9Ex)* Direct3DCreate9Ex = nullptr;
            if (library != nullptr)
            {
                Direct3DCreate9Ex = (decltype(Direct3DCreate9Ex))GetProcAddress(library, "Direct3DCreate9Ex");
                D3DPRESENT_PARAMETERS params = {};
                params.BackBufferWidth = 1;
                params.BackBufferHeight = 1;
                params.hDeviceWindow = dummyWindow;
                params.BackBufferCount = 1;
                params.Windowed = TRUE;
                params.SwapEffect = D3DSWAPEFFECT_DISCARD;

                if (Direct3DCreate9Ex != nullptr)
                {
                    Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D);
                    pD3D->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, NULL, reinterpret_cast<IDirect3DDevice9Ex**>(&pDevice));
                }
                else
                {
                    decltype(Direct3DCreate9)* Direct3DCreate9 = (decltype(Direct3DCreate9))GetProcAddress(library, "Direct3DCreate9");
                    if (Direct3DCreate9 != nullptr)
                    {
                        pD3D = reinterpret_cast<IDirect3D9Ex*>(Direct3DCreate9(D3D_SDK_VERSION));
                        pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, reinterpret_cast<IDirect3DDevice9**>(&pDevice));
                    }
                }
            }

            if (pDevice != nullptr)
            {
                SPDLOG_INFO("Hooked D3D9::Present to detect DX Version");

                dx9_hooked = true;
                HookDX9Present(reinterpret_cast<IDirect3DDevice9*>(pDevice), Direct3DCreate9Ex != nullptr);

                void** vTable = *reinterpret_cast<void***>(pDevice);
                decltype(&IDirect3DDevice9::Present) pfnPresent;
                decltype(&IDirect3DDevice9::Reset) pfnReset;
                decltype(&IDirect3DDevice9::EndScene) pfnEndScene;
                decltype(&IDirect3DDevice9Ex::PresentEx) pfnPresentEx = nullptr;

                (void*&)pfnPresent   = (void*)vTable[(int)IDirect3DDevice9VTable::Present];
                (void*&)pfnReset     = (void*)vTable[(int)IDirect3DDevice9VTable::Reset];
                (void*&)pfnEndScene  = (void*)vTable[(int)IDirect3DDevice9VTable::EndScene];
                if (Direct3DCreate9Ex != nullptr)
                {
                    (void*&)pfnPresentEx = (void*)vTable[(int)IDirect3DDevice9VTable::PresentEx];
                }

                dx9_hook = DX9_Hook::Inst();
                dx9_hook->loadFunctions(pfnPresent, pfnReset, pfnEndScene, pfnPresentEx);
            }
            else
            {
                SPDLOG_WARN("Failed to hook D3D9::Present to detect DX Version");
            }

            if (pDevice) pDevice->Release();
            if (pD3D) pD3D->Release();

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

    void hook_dx10()
    {
        if (!dx10_hooked)
        {
            IDXGISwapChain* pSwapChain = nullptr;
            ID3D10Device* pDevice = nullptr;
            HMODULE library = LoadLibraryA(DX10_Hook::DLL_NAME);
            if (library != nullptr)
            {
                decltype(D3D10CreateDeviceAndSwapChain)* D3D10CreateDeviceAndSwapChain =
                    (decltype(D3D10CreateDeviceAndSwapChain))GetProcAddress(library, "D3D10CreateDeviceAndSwapChain");
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
                    SwapChainDesc.OutputWindow = dummyWindow;
                    SwapChainDesc.SampleDesc.Count = 1;
                    SwapChainDesc.SampleDesc.Quality = 0;
                    SwapChainDesc.Windowed = TRUE;

                    D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice);
                }
            }
            if (pSwapChain != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");

                dx10_hooked = true;

                decltype(&IDXGISwapChain::Present) pfnPresent;
                decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
                decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;

                HookDXGIPresent(pSwapChain, pfnPresent, pfnResizeBuffers, pfnResizeTarget);

                dx10_hook = DX10_Hook::Inst();
                dx10_hook->loadFunctions(pfnPresent, pfnResizeBuffers, pfnResizeTarget);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }
            if (pDevice)pDevice->Release();
            if (pSwapChain)pSwapChain->Release();

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

    void hook_dx11()
    {
        if (!dx11_hooked)
        {
            IDXGISwapChain* pSwapChain = nullptr;
            ID3D11Device* pDevice = nullptr;
            HMODULE library = LoadLibraryA(DX11_Hook::DLL_NAME);
            if (library != nullptr)
            {
                decltype(D3D11CreateDeviceAndSwapChain)* D3D11CreateDeviceAndSwapChain =
                    (decltype(D3D11CreateDeviceAndSwapChain))GetProcAddress(library, "D3D11CreateDeviceAndSwapChain");
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
                    SwapChainDesc.OutputWindow = dummyWindow;
                    SwapChainDesc.SampleDesc.Count = 1;
                    SwapChainDesc.SampleDesc.Quality = 0;
                    SwapChainDesc.Windowed = TRUE;

                    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, NULL, D3D11_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice, NULL, NULL);
                }
            }
            if (pSwapChain != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");

                dx11_hooked = true;

                decltype(&IDXGISwapChain::Present) pfnPresent;
                decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
                decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;

                HookDXGIPresent(pSwapChain, pfnPresent, pfnResizeBuffers, pfnResizeTarget);

                dx11_hook = DX11_Hook::Inst();
                dx11_hook->loadFunctions(pfnPresent, pfnResizeBuffers, pfnResizeTarget);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }

            if (pDevice) pDevice->Release();
            if (pSwapChain) pSwapChain->Release();

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

    void hook_dx12()
    {
        if (!dx12_hooked)
        {
            IDXGIFactory4* pDXGIFactory = nullptr;
            IDXGISwapChain1* pSwapChain = nullptr;
            ID3D12CommandQueue* pCommandQueue = nullptr;
            ID3D12Device* pDevice = nullptr;

            HMODULE library = LoadLibraryA(DX12_Hook::DLL_NAME);
            if (library != nullptr)
            {
                decltype(D3D12CreateDevice)* D3D12CreateDevice =
                    (decltype(D3D12CreateDevice))GetProcAddress(library, "D3D12CreateDevice");

                if (D3D12CreateDevice != nullptr)
                {
                    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));

                    if (pDevice != nullptr)
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

                        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                        pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));

                        if (pCommandQueue != nullptr)
                        {
                            HMODULE dxgi = GetModuleHandleA("dxgi.dll");
                            if (dxgi != nullptr)
                            {
                                decltype(CreateDXGIFactory1)* CreateDXGIFactory1 = (decltype(CreateDXGIFactory1))GetProcAddress(dxgi, "CreateDXGIFactory1");
                                if (CreateDXGIFactory1 != nullptr)
                                {
                                    CreateDXGIFactory1(IID_PPV_ARGS(&pDXGIFactory));
                                    if (pDXGIFactory != nullptr)
                                    {
                                        pDXGIFactory->CreateSwapChainForHwnd(pCommandQueue, dummyWindow, &SwapChainDesc, NULL, NULL, &pSwapChain);
                                    }
                                }
                            }
                        }
                    }//if (pDevice != nullptr)
                }//if (D3D12CreateDevice != nullptr)
            }//if (library != nullptr)
            if (pCommandQueue != nullptr && pSwapChain != nullptr)
            {
                SPDLOG_INFO("Hooked IDXGISwapChain::Present to detect DX Version");

                dx12_hooked = true;

                decltype(&IDXGISwapChain::Present) pfnPresent;
                decltype(&IDXGISwapChain::ResizeBuffers) pfnResizeBuffers;
                decltype(&IDXGISwapChain::ResizeTarget) pfnResizeTarget;

                HookDXGIPresent(pSwapChain, pfnPresent, pfnResizeBuffers, pfnResizeTarget);

                void** vTable = *reinterpret_cast<void***>(pCommandQueue);
                decltype(&ID3D12CommandQueue::ExecuteCommandLists) pfnExecuteCommandLists;
                (void*&)pfnExecuteCommandLists = vTable[(int)ID3D12CommandQueueVTable::ExecuteCommandLists];

                dx12_hook = DX12_Hook::Inst();
                dx12_hook->loadFunctions(pfnPresent, pfnResizeBuffers, pfnResizeTarget, pfnExecuteCommandLists);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook IDXGISwapChain::Present to detect DX Version");
            }

            if (pSwapChain) pSwapChain->Release();
            if (pDXGIFactory) pDXGIFactory->Release();
            if (pCommandQueue) pCommandQueue->Release();
            if (pDevice) pDevice->Release();

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

    void hook_opengl()
    {
        if (!opengl_hooked)
        {
            HMODULE library = LoadLibraryA(OpenGL_Hook::DLL_NAME);
            decltype(::SwapBuffers)* wglSwapBuffers = nullptr;
            if (library != nullptr)
            {
                wglSwapBuffers = (decltype(wglSwapBuffers))GetProcAddress(library, "wglSwapBuffers");
            }
            if (wglSwapBuffers != nullptr)
            {
                SPDLOG_INFO("Hooked wglSwapBuffers to detect OpenGL");

                opengl_hooked = true;

                opengl_hook = OpenGL_Hook::Inst();
                opengl_hook->loadFunctions(wglSwapBuffers);

                HookwglSwapBuffers(wglSwapBuffers);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook wglSwapBuffers to detect OpenGL");
            }

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

    void hook_vulkan()
    {
        if (!vulkan_hooked)
        {
            HMODULE library = LoadLibraryA(Vulkan_Hook::DLL_NAME);
            decltype(::vkQueuePresentKHR)* vkQueuePresentKHR = nullptr;
            if (library != nullptr)
            {
                vkQueuePresentKHR = (decltype(vkQueuePresentKHR))GetProcAddress(library, "vkQueuePresentKHR");
            }
            if (vkQueuePresentKHR != nullptr)
            {
                SPDLOG_INFO("Hooked vkQueuePresentKHR to detect Vulkan");

                vulkan_hooked = true;
                
                vulkan_hook = Vulkan_Hook::Inst();
                vulkan_hook->loadFunctions();
                
                HookvkQueuePresentKHR(vkQueuePresentKHR);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook vkQueuePresentKHR to detect Vulkan");
            }

            if (library != nullptr)
            {
                FreeLibrary(library);
            }
        }
    }

public:
    Renderer_Hook* detect_renderer(std::chrono::milliseconds timeout)
    {
        std::pair<const char*, void(Renderer_Detector::*)()> libraries[]{
            std::pair<const char*, void(Renderer_Detector::*)()>{  DX12_Hook::DLL_NAME,& Renderer_Detector::hook_dx12},
            std::pair<const char*, void(Renderer_Detector::*)()>{  DX11_Hook::DLL_NAME,& Renderer_Detector::hook_dx11},
            std::pair<const char*, void(Renderer_Detector::*)()>{  DX10_Hook::DLL_NAME,& Renderer_Detector::hook_dx10},
            std::pair<const char*, void(Renderer_Detector::*)()>{   DX9_Hook::DLL_NAME,& Renderer_Detector::hook_dx9},
            std::pair<const char*, void(Renderer_Detector::*)()>{OpenGL_Hook::DLL_NAME,& Renderer_Detector::hook_opengl},
            std::pair<const char*, void(Renderer_Detector::*)()>{Vulkan_Hook::DLL_NAME,& Renderer_Detector::hook_vulkan},
        };

        {
            std::lock_guard<std::mutex> lk(renderer_mutex);
            if (detection_done)
            {
                return renderer_hook;
            }

            if (create_hwnd() == nullptr)
            {
                return nullptr;
            }
        }

        auto start_time = std::chrono::steady_clock::now();
        do
        {
            for (auto const& library : libraries)
            {
                if (Library::get_module_handle(library.first) != nullptr)
                {
                    std::lock_guard<std::mutex> lk(renderer_mutex);
                    (this->*library.second)();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
        } while (!detection_done && (timeout.count() == -1 || (std::chrono::steady_clock::now() - start_time) <= timeout));

        {
            std::lock_guard<std::mutex> lk(renderer_mutex);
            destroy_hwnd();

            detection_done = true;
            delete dx9_hook   ; dx9_hook    = nullptr;
            delete dx10_hook  ; dx10_hook   = nullptr;
            delete dx11_hook  ; dx11_hook   = nullptr;
            delete dx12_hook  ; dx12_hook   = nullptr;
            delete opengl_hook; opengl_hook = nullptr;
            delete vulkan_hook; vulkan_hook = nullptr;
        }

        return renderer_hook;
    }
};

Renderer_Detector* Renderer_Detector::instance = nullptr;

#elif defined(RENDERERDETECTOR_OS_LINUX)
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "linux/OpenGLX_Hook.h"

class Renderer_Detector
{
    static Renderer_Detector* instance;
public:
    static Renderer_Detector* Inst()
    {
        if (instance == nullptr)
        {
            instance = new Renderer_Detector;
        }
        return instance;
    }

    ~Renderer_Detector()
    {
        delete openglx_hook;
        //delete vulkan_hook;
    }

private:
    Renderer_Detector() :
        openglx_hooked(false),
        renderer_hook(nullptr),
        openglx_hook(nullptr),
        //vulkan_hook(nullptr),
        detection_done(false)
    {}

    std::mutex renderer_mutex;

    Base_Hook hooks;

    decltype(::glXSwapBuffers)* glXSwapBuffers;

    bool openglx_hooked;
    //bool vulkan_hooked;

    Renderer_Hook* renderer_hook;
    OpenGLX_Hook* openglx_hook;

    bool detection_done;

    static void MyglXSwapBuffers(Display* dpy, GLXDrawable drawable)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->glXSwapBuffers(dpy, drawable);
        if (inst->detection_done)
            return;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
        {
            inst->hooks.UnhookAll();
            inst->renderer_hook = static_cast<Renderer_Hook*>(Inst()->openglx_hook);
            inst->openglx_hook = nullptr;
            inst->detection_done = true;
        }
    }

    void HookglXSwapBuffers(decltype(::glXSwapBuffers)* _glXSwapBuffers)
    {
        glXSwapBuffers = _glXSwapBuffers;

        hooks.BeginHook();
        hooks.HookFunc(std::pair<void**, void*>{ (void**)&glXSwapBuffers, (void*)&MyglXSwapBuffers });
        hooks.EndHook();
    }

    void hook_openglx()
    {
        if (!openglx_hooked)
        {
            void* libGLX = dlopen(OpenGLX_Hook::DLL_NAME, RTLD_NOW);

            decltype(::glXSwapBuffers)* glXSwapBuffers = nullptr;

            if (libGLX != nullptr)
            {
                glXSwapBuffers = (decltype(glXSwapBuffers))dlsym(libGLX, "glXSwapBuffers");
            }
            if (glXSwapBuffers != nullptr)
            {
                SPDLOG_INFO("Hooked glXSwapBuffers to detect OpenGLX");

                openglx_hooked = true;

                openglx_hook = OpenGLX_Hook::Inst();
                openglx_hook->loadFunctions(glXSwapBuffers);

                HookglXSwapBuffers(glXSwapBuffers);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook glXSwapBuffers to detect OpenGLX");
            }

            if (libGLX != nullptr)
            {
                dlclose(libGLX);
            }
        }
    }

public:
    Renderer_Hook* detect_renderer(std::chrono::milliseconds timeout)
    {
        std::pair<const char*, void(Renderer_Detector::*)()> libraries[]{
            std::pair<const char*, void(Renderer_Detector::*)()>{OpenGLX_Hook::DLL_NAME,& Renderer_Detector::hook_openglx},
        };

        {
            std::lock_guard<std::mutex> lk(renderer_mutex);
            if (detection_done)
                return renderer_hook;
        }

        auto start_time = std::chrono::steady_clock::now();
        do
        {
            for (auto const& library : libraries)
            {
                if (Library::get_module_handle(library.first) != nullptr)
                {
                    std::lock_guard<std::mutex> lk(renderer_mutex);
                    (this->*library.second)();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
        } while (!detection_done && (timeout.count() == -1 || (std::chrono::steady_clock::now() - start_time) <= timeout));

        {
            std::lock_guard<std::mutex> lk(renderer_mutex);
            detection_done = true;
            delete openglx_hook; openglx_hook = nullptr;
            //delete vulkan_hook; vulkan_hook = nullptr;
        }

        return renderer_hook;
    }
};

Renderer_Detector* Renderer_Detector::instance = nullptr;

#elif defined(RENDERERDETECTOR_OS_APPLE)
#include "macosx/OpenGL_Hook.h"
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

class Renderer_Detector
{
   static Renderer_Detector* instance;
public:
   static Renderer_Detector* Inst()
   {
       if (instance == nullptr)
       {
           instance = new Renderer_Detector;
       }
       return instance;
   }

   ~Renderer_Detector()
   {
       delete opengl_hook;
   }

private:
   Renderer_Detector():
       opengl_hooked(false),
       renderer_hook(nullptr),
       opengl_hook(nullptr),
       detection_done(false)
   {}

   std::mutex renderer_mutex;

   Base_Hook hooks;

   decltype(::CGLFlushDrawable)* CGLFlushDrawable;

   bool opengl_hooked;

   Renderer_Hook* renderer_hook;
   OpenGL_Hook* opengl_hook;

   bool detection_done;

   static int64_t MyCGLFlushDrawable(CGLDrawable_t* glDrawable)
   {
       auto inst = Inst();
       std::lock_guard<std::mutex> lk(inst->renderer_mutex);
       int64_t res = inst->CGLFlushDrawable(glDrawable);

       if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(2, 0))
       {
           inst->hooks.UnhookAll();
           inst->renderer_hook = static_cast<Renderer_Hook*>(Inst()->opengl_hook);
           inst->opengl_hook = nullptr;
           inst->detection_done = true;
       }

       return res;
   }

   void HookglFlushDrawable(decltype(::CGLFlushDrawable)* _CGLFlushDrawable)
   {
       CGLFlushDrawable = _CGLFlushDrawable;

       hooks.BeginHook();
       hooks.HookFunc(std::pair<void**, void*>{ (void**)&CGLFlushDrawable, (void*)&MyCGLFlushDrawable });
       hooks.EndHook();
   }

   void hook_opengl(const char* library_path)
   {
       if (!opengl_hooked)
       {
           Library libOpenGL;
           libOpenGL.load_library(library_path, false);

           decltype(::CGLFlushDrawable)* CGLFlushDrawable = nullptr;

           if (libOpenGL != nullptr)
           {
               CGLFlushDrawable = libOpenGL.get_symbol<decltype(CGLFlushDrawable)>("CGLFlushDrawable");
           }
           if (CGLFlushDrawable != nullptr)
           {
               SPDLOG_INFO("Hooked CGLFlushDrawable to detect OpenGL");

               opengl_hooked = true;

               opengl_hook = OpenGL_Hook::Inst();
               opengl_hook->loadFunctions(CGLFlushDrawable);

               HookglFlushDrawable(CGLFlushDrawable);
           }
           else
           {
               SPDLOG_WARN("Failed to Hook CGLFlushDrawable to detect OpenGL");
           }

           if (libOpenGL != nullptr)
           {
               dlclose(libOpenGL);
           }
       }
   }

public:
   Renderer_Hook* detect_renderer(std::chrono::milliseconds timeout)
   {
       std::pair<const char*, void(Renderer_Detector::*)(const char*)> libraries[]{
           std::pair<const char*, void(Renderer_Detector::*)(const char*)>{OpenGL_HookConsts::dll_name, &Renderer_Detector::hook_opengl}
       };

       {
           std::lock_guard<std::mutex> lk(renderer_mutex);
           if (detection_done)
               return renderer_hook;
       }

       auto start_time = std::chrono::steady_clock::now();
       do
       {
           for (auto const& library : libraries)
           {
               void* libHandle = Library::get_module_handle(library.first);
               if (libHandle != nullptr)
               {
                   std::lock_guard<std::mutex> lk(renderer_mutex);
                   std::string OpenGLPath = Library::get_module_path(libHandle);
                   (this->*library.second)(OpenGLPath.c_str());
               }
           }
           std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
       } while (!detection_done && (timeout.count() == -1 || (std::chrono::steady_clock::now() - start_time) <= timeout));

       {
           std::lock_guard<std::mutex> lk(renderer_mutex);
           detection_done = true;
           delete opengl_hook; opengl_hook = nullptr;
           //delete vulkan_hook; vulkan_hook = nullptr;
       }

       return renderer_hook;
   }
};

Renderer_Detector* Renderer_Detector::instance = nullptr;

#endif

std::future<Renderer_Hook*> detect_renderer(std::chrono::milliseconds timeout)
{
    return std::async(std::launch::async, &Renderer_Detector::detect_renderer, Renderer_Detector::Inst(), timeout);
}
