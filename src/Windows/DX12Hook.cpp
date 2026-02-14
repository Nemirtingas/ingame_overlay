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

#include "DX12Hook.h"
#include "WindowsHook.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_dx12.h>

namespace InGameOverlay {

struct DX12Texture_t : RendererTexture_t {
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle = {};
    ID3D12Resource* pTexture = nullptr;
    uint32_t Id = 0;
};

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX12Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
} } while(0)

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&DX12Hook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

DX12Hook_t* DX12Hook_t::_Instance = nullptr;

static_assert(sizeof(D3D12_GPU_DESCRIPTOR_HANDLE::ptr) == sizeof(ImTextureID), "sizeof(D3D12_GPU_DESCRIPTOR_HANDLE::ptr) must match sizeof(ImTextureID)");

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

static inline uint32_t MakeHeapId(uint32_t heapIndex, uint32_t usedIndex)
{
    return (heapIndex << 16) | usedIndex;
}

static inline uint32_t GetHeapIndex(uint32_t heapId)
{
    return heapId >> 16;
}

static inline uint32_t GetHeapUsedIndex(uint32_t heapId)
{
    return heapId & 0xffff;
}

bool DX12Hook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_ID3D12DeviceRelease == nullptr || _IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr || _ID3D12CommandQueueExecuteCommandLists == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook DirectX 12: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(ID3D12DeviceRelease);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainPresent);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeTarget);
        TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChainResizeBuffers);
        TRY_HOOK_FUNCTION_OR_FAIL(ID3D12CommandQueueExecuteCommandLists);

        if (_IDXGISwapChain1Present1 != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChain1Present1);

        if (_IDXGISwapChain3ResizeBuffers1 != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(IDXGISwapChain3ResizeBuffers1);

        EndHook();

        INGAMEOVERLAY_INFO("Hooked DirectX 12");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void DX12Hook_t::HideAppInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideAppInputs(hide);
}

void DX12Hook_t::HideOverlayInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
}

bool DX12Hook_t::IsStarted()
{
    return _Hooked;
}

bool DX12Hook_t::_AllocShaderRessourceViewHeap()
{
    ID3D12DescriptorHeap *pHeap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = ShaderResourceViewHeap_t::HeapSize;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)) != S_OK)
        return false;

    _ShaderResourceViewHeapDescriptors.emplace_back(pHeap);
    _ShaderResourceViewHeaps.emplace_back();
    return true;
}

DX12Hook_t::ShaderRessourceView_t DX12Hook_t::_GetFreeShaderRessourceViewFromHeap(uint32_t heapIndex)
{
    ShaderRessourceView_t result{};
    UINT inc = _Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto& heap = _ShaderResourceViewHeaps[heapIndex];
    for (uint32_t usedIndex = 0; usedIndex < heap.HeapBitmap.size(); ++usedIndex)
    {
        if (!heap.HeapBitmap[usedIndex])
        {
            heap.HeapBitmap[usedIndex] = true;
            ++heap.UsedHeap;
            result.CpuHandle.ptr = _ShaderResourceViewHeapDescriptors[heapIndex].Heap->GetCPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.GpuHandle.ptr = _ShaderResourceViewHeapDescriptors[heapIndex].Heap->GetGPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.Id = MakeHeapId(heapIndex, usedIndex);
            break;
        }
    }

    return result;
}

DX12Hook_t::ShaderRessourceView_t DX12Hook_t::_GetFreeShaderRessourceView()
{
    for (uint32_t heapIndex = 0; heapIndex < _ShaderResourceViewHeaps.size(); ++heapIndex)
    {
        if (_ShaderResourceViewHeaps[heapIndex].UsedHeap != ShaderResourceViewHeap_t::HeapSize)
            return _GetFreeShaderRessourceViewFromHeap(heapIndex);
    }

    if (_AllocShaderRessourceViewHeap())
        return _GetFreeShaderRessourceViewFromHeap((uint32_t)_ShaderResourceViewHeaps.size()-1);

    return ShaderRessourceView_t{ 0, 0, ShaderRessourceView_t::InvalidId };
}

void DX12Hook_t::_ReleaseShaderRessourceView(uint32_t id)
{
    auto& heap = _ShaderResourceViewHeaps[GetHeapIndex(id)];
    heap.HeapBitmap[GetHeapUsedIndex(id)] = false;
    --heap.UsedHeap;
}

ID3D12CommandQueue* DX12Hook_t::_FindCommandQueueFromSwapChain(IDXGISwapChain* pSwapChain)
{
    constexpr int MaxRetries = 10;
    ID3D12CommandQueue* pCommandQueue = nullptr;
    ID3D12CommandQueue* pHookedCommandQueue = _CommandQueue;

    if (pHookedCommandQueue == nullptr)
        return nullptr;

    if (_CommandQueueOffset == 0)
    {
        for (size_t i = 0; i < 1024; ++i)
        {
            if (*reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + i) == pHookedCommandQueue)
            {
                INGAMEOVERLAY_INFO("Found IDXGISwapChain::ppCommandQueue at offset {}.", i);
                _CommandQueueOffset = i;
                break;
            }
        }
    }

    if (_CommandQueueOffset != 0)
    {
        pCommandQueue = *reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + _CommandQueueOffset);
    }
    else if (_CommandQueueOffsetRetries <= MaxRetries)
    {
        if (++_CommandQueueOffsetRetries >= MaxRetries)
        {
            INGAMEOVERLAY_INFO("Failed to find IDXGISwapChain::ppCommandQueue, fallback to ID3D12CommandQueue::ExecuteCommandLists");
        }
    }
    else
    {
        pCommandQueue = pHookedCommandQueue;
    }

    return pCommandQueue;
}

void DX12Hook_t::_UpdateHookDeviceRefCount()
{
    switch (_HookState)
    {
        case OverlayHookState::Removing: _HookDeviceRefCount =
            + 1 // _Device->AddRef
            ;
            break;
        // ImGui:
        //   PipelineState
        //   FontTexture
        //   (VertexBuffer + IndexBuffer) * FrameCount
        case OverlayHookState::Ready: _HookDeviceRefCount =
            + 6 // 6 From swapchain
            + 1 // ID3D12RootSignature (singleton per device)
            + 2 // 2 From lazy initialization of DX12, the device gets 2 refs randomly in ImGui_ImplDX12_CreateDeviceObjects 
            + 1 // _Device->AddRef
            + 1 // RenderTargetViewDescriptorHeap
            + (_OverlayFrames.size() * 2) + 1 // FrameCount * (RenderTargetCommandAllocator + RenderTargetCommandList)
            + 1 // ImageFence
            + 1 // ImageCommandQueue
            + 1 // ImageCommandAllocator
            + 1 // ImageCommandList
            + _ShaderResourceViewHeapDescriptors.size()
            + _ImageResourcesToRelease.size()
            + std::count_if(_ImageResources.begin(), _ImageResources.end(), [](std::shared_ptr<RendererTexture_t> tex) { return tex->LoadStatus == RendererTextureStatus_e::Loaded; })
            + 1 // ImGui PipelineState
            + 1 // ImGui FontTexture
            + (_OverlayFrames.size() * 2) // ImGui VertexBuffer + IndexBuffer
            ;
            break;
    }
}

bool DX12Hook_t::_CreateRenderTargets(IDXGISwapChain* pSwapChain)
{
    IDXGISwapChain3* pSwapChain3 = nullptr;
    DXGI_SWAP_CHAIN_DESC sc_desc;

    // Happens when the functions have been hooked, but the DX hook is not setup yet.
    if (_Device == nullptr)
        return false;

    pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
    if (pSwapChain3 == nullptr)
        return false;

    pSwapChain3->GetDesc(&sc_desc);

    UINT bufferCount = sc_desc.BufferCount;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = bufferCount;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_RenderTargetViewDescriptorHeap)) != S_OK)
        {
            pSwapChain3->Release();
            return false;
        }

        SIZE_T rtvDescriptorSize = _Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _RenderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        _OverlayFrames.resize(bufferCount);
        for (UINT i = 0; i < bufferCount; ++i)
        {
            auto& frame = _OverlayFrames[i];

            if (_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.CommandAllocator)) != S_OK || frame.CommandAllocator == nullptr)
            {
                _DestroyRenderTargets();
                pSwapChain3->Release();
                return false;
            }

            if (_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame.CommandAllocator, NULL, IID_PPV_ARGS(&frame.CommandList)) != S_OK ||
                frame.CommandList == nullptr || frame.CommandList->Close() != S_OK)
            {
                _DestroyRenderTargets();
                pSwapChain3->Release();
                return false;
            }

            if (pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&frame.BackBuffer)) != S_OK || frame.BackBuffer == nullptr)
            {
                _DestroyRenderTargets();
                pSwapChain3->Release();
                return false;
            }

            _Device->CreateRenderTargetView(frame.BackBuffer, NULL, rtvHandle);
            frame.RenderTarget = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    pSwapChain3->Release();

    return true;
}

void DX12Hook_t::_DestroyRenderTargets()
{
    _OverlayFrames.clear();
    SafeRelease(_RenderTargetViewDescriptorHeap);
}

bool DX12Hook_t::_CreateImageObjects()
{
    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 1;

    _ImageEvent = CreateEventW(0, 0, 0, 0);
    if (_ImageEvent == NULL)
        goto on_fail;

    hr = _Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_ImageFence));
    if (!SUCCEEDED(hr))
        goto on_fail;

    hr = _Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_ImageCommandQueue));
    if (!SUCCEEDED(hr))
        goto on_fail;
    
    hr = _Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_ImageCommandAllocator));
    if (!SUCCEEDED(hr))
        goto on_fail;
    
    hr = _Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _ImageCommandAllocator, NULL, IID_PPV_ARGS(&_ImageCommandList));
    if (!SUCCEEDED(hr))
        goto on_fail;

    return true;

on_fail:
    _DestroyImageObjects();

    return false;
}

void DX12Hook_t::_DestroyImageObjects()
{
    if (_ImageEvent != NULL)
    {
        CloseHandle(_ImageEvent);
        _ImageEvent = NULL;
    }
    SafeRelease(_ImageCommandList);
    SafeRelease(_ImageCommandAllocator);
    SafeRelease(_ImageCommandQueue);
    SafeRelease(_ImageFence);
}

void DX12Hook_t::_ResetRenderState(OverlayHookState state)
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
            ImGui_ImplDX12_Shutdown();
            WindowsHook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();
            _ImageResourcesToLoad.clear();
            _ImageResourcesToRelease.clear();
            _DestroyImageObjects();
            _ShaderResourceViewHeaps.clear();
            _ShaderResourceViewHeapDescriptors.clear();

            _DestroyRenderTargets();

            SafeRelease(_Device);
            _CommandQueueOffset = 0;
            _CommandQueueOffsetRetries = 0;
            _CommandQueue = nullptr;
            break;

        case OverlayHookState::Reset:
            _DestroyRenderTargets();

    }
    
    if (state == OverlayHookState::Removing)
        --_DeviceReleasing;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX12Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pCommandQueue, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST)
        return;

    IDXGISwapChain3* pSwapChain3 = nullptr;
    DXGI_SWAP_CHAIN_DESC sc_desc;
    pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
    if (pSwapChain3 == nullptr)
        return;

    pSwapChain3->GetDesc(&sc_desc);

    if(_HookState == OverlayHookState::Removing)
    {
        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();
        _Device = nullptr;
        if (pSwapChain3->GetDevice(IID_PPV_ARGS(&_Device)) != S_OK)
        {
            pSwapChain3->Release();
            return;
        }

        if (!_AllocShaderRessourceViewHeap())
        {
            _Device->Release();
            pSwapChain3->Release();
            return;
        }

        if (!_CreateRenderTargets(pSwapChain))
        {
            _Device->Release();
            _ShaderResourceViewHeaps.clear();
            _ShaderResourceViewHeapDescriptors.clear();
            pSwapChain3->Release();
            return;
        }

        if (!_CreateImageObjects())
        {
            _DestroyRenderTargets();
            _Device->Release();
            _ShaderResourceViewHeaps.clear();
            _ShaderResourceViewHeapDescriptors.clear();
            pSwapChain3->Release();
            return;
        }

        auto shaderRessourceView = _GetFreeShaderRessourceView();

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.CommandQueue = pCommandQueue;
        initInfo.Device = _Device;
        initInfo.NumFramesInFlight = sc_desc.BufferCount;
        initInfo.RTVFormat = sc_desc.BufferDesc.Format;
        initInfo.DSVFormat = sc_desc.BufferDesc.Format;
        initInfo.handleIncrement = _Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        initInfo.UserData = this;
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
        {
            auto* _this = reinterpret_cast<DX12Hook_t*>(info->UserData);
            auto shaderResource = _this->_GetFreeShaderRessourceView();
            _this->_ImGuiFontTextureId = shaderResource.Id;
            *out_cpu_desc_handle = shaderResource.CpuHandle;
            *out_gpu_desc_handle = shaderResource.GpuHandle;
        };
        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle)
        {
            auto* _this = reinterpret_cast<DX12Hook_t*>(info->UserData);
            _this->_ReleaseShaderRessourceView(_this->_ImGuiFontTextureId);
        };

        ImGui_ImplDX12_Init(&initInfo);

        WindowsHook_t::Inst()->SetInitialWindowSize(sc_desc.OutputWindow);

        if (!ImGui_ImplDX12_CreateDeviceObjects())
        {
            ImGui_ImplDX12_Shutdown();
            _DestroyRenderTargets();
            _Device->Release();
            _ShaderResourceViewHeaps.clear();
            _ShaderResourceViewHeapDescriptors.clear();
            pSwapChain3->Release();
            return;
        }

        _ResetRenderState(OverlayHookState::Ready);
    }

    if (ImGui_ImplDX12_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(sc_desc.OutputWindow))
    {
        auto& frame = _OverlayFrames[pSwapChain3->GetCurrentBackBufferIndex()];

        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot(frame);

        if (_ImGuiFontAtlas != nullptr)
        {
            const bool has_textures = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
            ImFontAtlasUpdateNewFrame(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas), ImGui::GetFrameCount(), has_textures);
        }

        ++_CurrentFrame;
        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();
        _ReleaseResources();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = frame.BackBuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        frame.CommandAllocator->Reset();
        frame.CommandList->Reset(frame.CommandAllocator, NULL);
        frame.CommandList->ResourceBarrier(1, &barrier);
        frame.CommandList->OMSetRenderTargets(1, &frame.RenderTarget, FALSE, NULL);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), frame.CommandList, &_ShaderResourceViewHeapDescriptors[0].Heap, (int)_ShaderResourceViewHeapDescriptors.size(), ShaderResourceViewHeap_t::HeapSize);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        frame.CommandList->ResourceBarrier(1, &barrier);
        frame.CommandList->Close();

        pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&frame.CommandList);

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot(frame);
    }

    pSwapChain3->Release();
}

void DX12Hook_t::_LoadResources()
{
    HRESULT hr;

    if (_ImageResourcesToLoad.empty())
        return;

    D3D12_HEAP_PROPERTIES defaultProps{};
    defaultProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_PROPERTIES uploadProps{};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    struct ValidTexture_t
    {
        std::shared_ptr<DX12Texture_t> Resource;
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
        if (!r) continue;

        validResources.push_back(ValidTexture_t{
            std::static_pointer_cast<DX12Texture_t>(r),
            param.Data,
            param.Width,
            param.Height
        });
    }

    if (!validResources.empty())
    {
        UINT64 totalUploadSize = 0;
        std::vector<UINT> rowPitches;
        rowPitches.reserve(validResources.size());
        for (auto& tex : validResources)
        {
            UINT pitch = (tex.Width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            rowPitches.push_back(pitch);
            totalUploadSize += tex.Height * pitch;
        }

        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Alignment = 0;
        uploadDesc.Width = totalUploadSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource* uploadBuffer = nullptr;
        hr = _Device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
        IM_ASSERT(SUCCEEDED(hr));

        void* mapped = nullptr;
        D3D12_RANGE range{ 0, static_cast<SIZE_T>(totalUploadSize) };
        hr = uploadBuffer->Map(0, &range, &mapped);
        IM_ASSERT(SUCCEEDED(hr));

        UINT64 offset = 0;
        std::vector<D3D12_TEXTURE_COPY_LOCATION> srcLocations;
        srcLocations.reserve(validResources.size());

        for (size_t i = 0; i < validResources.size(); ++i)
        {
            auto& tex = validResources[i];
            UINT pitch = rowPitches[i];

            for (uint32_t y = 0; y < tex.Height; ++y)
            {
                memcpy((uint8_t*)mapped + offset + y * pitch,
                    (const uint8_t*)tex.Data + y * tex.Width * 4,
                    tex.Width * 4);
            }

            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource = uploadBuffer;
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint.Offset = offset;
            srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srcLoc.PlacedFootprint.Footprint.Width = tex.Width;
            srcLoc.PlacedFootprint.Footprint.Height = tex.Height;
            srcLoc.PlacedFootprint.Footprint.Depth = 1;
            srcLoc.PlacedFootprint.Footprint.RowPitch = pitch;

            srcLocations.push_back(srcLoc);
            offset += tex.Height * pitch;
        }

        uploadBuffer->Unmap(0, &range);

        std::vector<ID3D12Resource*> createdTextures;
        createdTextures.reserve(validResources.size());

        for (size_t i = 0; i < validResources.size(); ++i)
        {
            auto& tex = validResources[i];

            D3D12_RESOURCE_DESC texDesc{};
            texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texDesc.Width = tex.Width;
            texDesc.Height = tex.Height;
            texDesc.DepthOrArraySize = 1;
            texDesc.MipLevels = 1;
            texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            ID3D12Resource* pTexture = nullptr;
            hr = _Device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pTexture));
            IM_ASSERT(SUCCEEDED(hr));
            createdTextures.push_back(pTexture);

            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource = pTexture;
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = 0;

            _ImageCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLocations[i], nullptr);

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = pTexture;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            _ImageCommandList->ResourceBarrier(1, &barrier);

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            _Device->CreateShaderResourceView(pTexture, &srvDesc, tex.Resource->CpuHandle);
        }

        hr = _ImageCommandList->Close();
        IM_ASSERT(SUCCEEDED(hr));
        _ImageCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&_ImageCommandList);

        hr = _ImageCommandQueue->Signal(_ImageFence, ++_ImageFenceValue);
        IM_ASSERT(SUCCEEDED(hr));
        _ImageFence->SetEventOnCompletion(_ImageFenceValue, _ImageEvent);
        WaitForSingleObject(_ImageEvent, INFINITE);

        hr = _ImageCommandAllocator->Reset();
        IM_ASSERT(SUCCEEDED(hr));
        hr = _ImageCommandList->Reset(_ImageCommandAllocator, nullptr);
        IM_ASSERT(SUCCEEDED(hr));

        for (size_t i = 0; i < validResources.size(); ++i)
        {
            validResources[i].Resource->pTexture = createdTextures[i];
            validResources[i].Resource->LoadStatus = RendererTextureStatus_e::Loaded;
        }

        SafeRelease(uploadBuffer);
    }

    _ImageResourcesToLoad.erase(_ImageResourcesToLoad.begin(),
        _ImageResourcesToLoad.begin() + loadParameterCount);

    _UpdateHookDeviceRefCount();
}

void DX12Hook_t::_ReleaseResources()
{
    if (_ImageResourcesToRelease.empty())
        return;

    for (auto it = _ImageResourcesToRelease.begin(); it != _ImageResourcesToRelease.end();)
    {
        if ((it->ReleaseFrame + _OverlayFrames.size()) < _CurrentFrame)
        {
            it = _ImageResourcesToRelease.erase(it);
        }
        else
        {
            ++it;
        }
    }

    _UpdateHookDeviceRefCount();
}

void DX12Hook_t::_HandleScreenshot(DX12Frame_t& frame)
{
    bool result = false;

    ID3D12CommandAllocator* pCommandAlloc = nullptr;
    ID3D12GraphicsCommandList* pCommandList = nullptr;
    ID3D12Fence* pFence = nullptr;
    ID3D12Resource* pCopySource = nullptr;
    ID3D12Resource* pStaging = nullptr;

    D3D12_RESOURCE_DESC desc = frame.BackBuffer->GetDesc();

    D3D12_HEAP_PROPERTIES sourceHeapProperties;
    D3D12_HEAP_PROPERTIES defaultHeapProperties{};
    D3D12_HEAP_PROPERTIES readBackHeapProperties{};

    D3D12_RESOURCE_DESC bufferDesc = {};
    D3D12_RESOURCE_BARRIER barrier = {};

    D3D12_TEXTURE_COPY_LOCATION copyDest{};
    D3D12_TEXTURE_COPY_LOCATION copySrc{};

    UINT64 totalSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;

    BYTE* pMappedMemory = nullptr;
    ScreenshotCallbackParameter_t screenshot;

    _Device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSize, &totalSize);

    HRESULT hr = frame.BackBuffer->GetHeapProperties(&sourceHeapProperties, nullptr);
    if (SUCCEEDED(hr) && sourceHeapProperties.Type == D3D12_HEAP_TYPE_READBACK)
    {
        pCopySource = frame.BackBuffer;
        goto readback;
    }

    // Create a command allocator
    hr = _Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAlloc));
    if (FAILED(hr) || pCommandAlloc == nullptr)
        goto cleanup;

    // Spin up a new command list
    hr = _Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCommandAlloc, nullptr, IID_PPV_ARGS(&pCommandList));
    if (FAILED(hr) || pCommandList == nullptr)
        goto cleanup;

    // Create a fence    
    hr = _Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    if (FAILED(hr) || pFence == nullptr)
        goto cleanup;

    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    readBackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;

    // Readback resources must be buffers
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.Height = 1;
    bufferDesc.Width = layout.Footprint.RowPitch * desc.Height;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = frame.BackBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // Create a staging texture
    hr = _Device->CreateCommittedResource(&readBackHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pStaging));
    if (FAILED(hr) || pStaging == nullptr)
        goto cleanup;

    pCopySource = pStaging;

readback:
    // Transition the resource if necessary
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    pCommandList->ResourceBarrier(1, &barrier);

    // Get the copy target location
    copyDest.pResource = pCopySource;
    copyDest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    copyDest.PlacedFootprint.Footprint.Width = static_cast<UINT>(desc.Width);
    copyDest.PlacedFootprint.Footprint.Height = desc.Height;
    copyDest.PlacedFootprint.Footprint.Depth = 1;
    copyDest.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(layout.Footprint.RowPitch);
    copyDest.PlacedFootprint.Footprint.Format = desc.Format;

    copySrc.pResource = frame.BackBuffer;
    copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copySrc.SubresourceIndex = 0;

    // Copy the texture
    pCommandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);

    // Transition the source resource to the next state
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    pCommandList->ResourceBarrier(1, &barrier);

    hr = pCommandList->Close();
    if (FAILED(hr))
        goto cleanup;

    // Execute the command list
    _CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCommandList);

    // Signal the fence
    hr = _CommandQueue->Signal(pFence, 1);
    if (FAILED(hr))
        goto cleanup;

    // Block until the copy is complete
    while (pFence->GetCompletedValue() < 1)
        SwitchToThread();

    hr = pStaging->Map(0, nullptr, (void**)&pMappedMemory);
    if (FAILED(hr))
        goto cleanup;

    screenshot.Width = desc.Width;
    screenshot.Height = desc.Height;
    screenshot.Pitch = layout.Footprint.RowPitch;
    screenshot.Data = reinterpret_cast<void*>(pMappedMemory);
    screenshot.Format = RendererFormatToScreenshotFormat(desc.Format);

    _SendScreenshot(&screenshot);

    pStaging->Unmap(0, nullptr);

    result = true;

cleanup:

    SafeRelease(pStaging);
    SafeRelease(pFence);
    SafeRelease(pCommandList);
    SafeRelease(pCommandAlloc);

    if (!result)
        _SendScreenshot(nullptr);
}

ULONG STDMETHODCALLTYPE DX12Hook_t::_MyID3D12DeviceRelease(IUnknown* _this)
{
    auto inst = DX12Hook_t::Inst();
    auto result = (_this->*inst->_ID3D12DeviceRelease)();

    if (_this == inst->_Device)
    {
        INGAMEOVERLAY_INFO("ID3D12Device::Release: RefCount = {}, Our removal threshold = {}", result, inst->_HookDeviceRefCount);

        if (inst->_DeviceReleasing == 0 && result <= inst->_HookDeviceRefCount)
            inst->_ResetRenderState(OverlayHookState::Removing);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::Present");
    auto inst = DX12Hook_t::Inst();

    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue, Flags);

    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeBuffers");
    auto inst = DX12Hook_t::Inst();
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

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain::ResizeTarget");
    auto inst = DX12Hook_t::Inst();
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

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain1::Present1");
    auto inst = DX12Hook_t::Inst();
    
    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue, Flags);

    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChain3ResizeBuffers1(IDXGISwapChain3* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    INGAMEOVERLAY_INFO("IDXGISwapChain3::ResizeBuffers1");
    auto inst = DX12Hook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_Device != nullptr && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = true;
        inst->_ResetRenderState(OverlayHookState::Reset);
    }
    auto r = (_this->*inst->_IDXGISwapChain3ResizeBuffers1)(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    if (createRenderTargets)
    {
        inst->_ResetRenderState(inst->_CreateRenderTargets(_this)
            ? OverlayHookState::Ready
            : OverlayHookState::Removing);
    }

    return r;
}

void STDMETHODCALLTYPE DX12Hook_t::_MyID3D12CommandQueueExecuteCommandLists(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    INGAMEOVERLAY_INFO("ID3D12CommandQueue::ExecuteCommandLists");
    auto inst = DX12Hook_t::Inst();
    if (_this->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        inst->_CommandQueue = _this;

    (_this->*inst->_ID3D12CommandQueueExecuteCommandLists)(NumCommandLists, ppCommandLists);
}

DX12Hook_t::DX12Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _DeviceReleasing(0),
    _CommandQueueOffsetRetries(0),
    _CommandQueueOffset(0),
    _CommandQueue(nullptr),
    _Device(nullptr),
    _HookDeviceRefCount(0),
    _HookState(OverlayHookState::Removing),
    _RenderTargetViewDescriptorHeap(nullptr),
    _ImageEvent(nullptr),
    _ImageFenceValue(0),
    _ImageFence(nullptr),
    _ImageCommandQueue(nullptr),
    _ImageCommandAllocator(nullptr),
    _ImageCommandList(nullptr),
    _ImGuiFontTextureId(ShaderRessourceView_t::InvalidId),
    _ImGuiFontAtlas(nullptr),
    _ID3D12DeviceRelease(nullptr),
    _IDXGISwapChainPresent(nullptr),
    _IDXGISwapChainResizeBuffers(nullptr),
    _IDXGISwapChainResizeTarget(nullptr),
    _IDXGISwapChain3ResizeBuffers1(nullptr),
    _ID3D12CommandQueueExecuteCommandLists(nullptr),
    _IDXGISwapChain1Present1(nullptr)
{
}

DX12Hook_t::~DX12Hook_t()
{
    INGAMEOVERLAY_INFO("DX12 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

DX12Hook_t* DX12Hook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new DX12Hook_t();

    return _Instance;
}

const char* DX12Hook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t DX12Hook_t::GetRendererHookType() const
{
    return RendererHookType_t::DirectX12;
}

void DX12Hook_t::LoadFunctions(
    decltype(_ID3D12DeviceRelease) releaseFcn,
    decltype(_IDXGISwapChainPresent) presentFcn,
    decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
    decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
    decltype(_IDXGISwapChain1Present1) present1Fcn,
    decltype(_IDXGISwapChain3ResizeBuffers1) resizeBuffers1Fcn,
    decltype(_ID3D12CommandQueueExecuteCommandLists) executeCommandListsFcn)
{
    _ID3D12DeviceRelease = releaseFcn;

    _IDXGISwapChainPresent = presentFcn;
    _IDXGISwapChainResizeBuffers = resizeBuffersFcn;
    _IDXGISwapChainResizeTarget = resizeTargetFcn;

    _IDXGISwapChain1Present1 = present1Fcn;

    _IDXGISwapChain3ResizeBuffers1 = resizeBuffers1Fcn;

    _ID3D12CommandQueueExecuteCommandLists = executeCommandListsFcn;
}

std::weak_ptr<RendererTexture_t> DX12Hook_t::AllocImageResource()
{
    auto shaderRessourceView = _GetFreeShaderRessourceView();
    
    if (shaderRessourceView.Id == ShaderRessourceView_t::InvalidId)
        return std::weak_ptr<RendererTexture_t>{};
    
    DX12Texture_t* pTextureData = new DX12Texture_t;
    pTextureData->ImGuiTextureId = shaderRessourceView.GpuHandle.ptr;
    pTextureData->CpuHandle = shaderRessourceView.CpuHandle;
    pTextureData->Id = shaderRessourceView.Id;
    
    auto image = std::shared_ptr<RendererTexture_t>((RendererTexture_t*)pTextureData, [this](RendererTexture_t* handle)
    {
        if (handle != nullptr)
        {
            auto pTextureData = static_cast<DX12Texture_t*>(handle);
            SafeRelease(pTextureData->pTexture);
            _ReleaseShaderRessourceView(pTextureData->Id);
    
            delete pTextureData;
        }
    });
    
    _ImageResources.emplace(image);
    
    return image;
}

void DX12Hook_t::LoadImageResource(RendererTextureLoadParameter_t& loadParameter)
{
    _ImageResourcesToLoad.emplace_back(loadParameter);
}

void DX12Hook_t::ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource)
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