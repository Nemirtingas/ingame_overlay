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

#include "DX12_Hook.h"
#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(PVOID&)NAME, &DX12_Hook::My##NAME))) { \
    SPDLOG_ERROR("Failed to hook {}", #NAME);\
    return false;\
} } while(0)

DX12_Hook* DX12_Hook::_inst = nullptr;

template<typename T>
static inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
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

bool DX12_Hook::StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (Present == nullptr || ResizeTarget == nullptr || ResizeBuffers == nullptr || ExecuteCommandLists == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 12: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(Present);
        TRY_HOOK_FUNCTION(ResizeTarget);
        TRY_HOOK_FUNCTION(ResizeBuffers);
        TRY_HOOK_FUNCTION(ExecuteCommandLists);

        if (Present1 != nullptr)
            TRY_HOOK_FUNCTION(Present1);

        if (ResizeBuffers1 != nullptr)
            TRY_HOOK_FUNCTION(ResizeBuffers1);

        EndHook();

        SPDLOG_INFO("Hooked DirectX 12");
        _Hooked = true;
        _ImGuiFontAtlas = imgui_font_atlas;
    }
    return true;
}

void DX12_Hook::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideAppInputs(hide);
    }
}

void DX12_Hook::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideOverlayInputs(hide);
    }
}

bool DX12_Hook::IsStarted()
{
    return _Hooked;
}

void DX12_Hook::_ReleaseDX12Ressources()
{
    ShaderRessourceViewHeaps.clear();
    ShaderRessourceViewHeapDescriptors.clear();
    SafeRelease(pRtvDescHeap);
    SafeRelease(pDevice);
}

bool DX12_Hook::_AllocShaderRessourceViewHeap()
{
    ID3D12DescriptorHeap *pHeap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = ShaderRessourceViewHeap_t::HeapSize;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)) != S_OK)
        return false;

    ShaderRessourceViewHeapDescriptors.emplace_back(pHeap);
    ShaderRessourceViewHeaps.emplace_back();
    return true;
}

DX12_Hook::ShaderRessourceView_t DX12_Hook::_GetFreeShaderRessourceViewFromHeap(uint32_t heapIndex)
{
    ShaderRessourceView_t result{};
    UINT inc = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto& heap = ShaderRessourceViewHeaps[heapIndex];
    for (uint32_t usedIndex = 0; usedIndex < heap.HeapBitmap.size(); ++usedIndex)
    {
        if (!heap.HeapBitmap[usedIndex])
        {
            heap.HeapBitmap[usedIndex] = true;
            ++heap.UsedHeap;
            result.CpuHandle.ptr = ShaderRessourceViewHeapDescriptors[heapIndex].Heap->GetCPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.GpuHandle.ptr = ShaderRessourceViewHeapDescriptors[heapIndex].Heap->GetGPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.Id = MakeHeapId(heapIndex, usedIndex);
            break;
        }
    }

    return result;
}

DX12_Hook::ShaderRessourceView_t DX12_Hook::_GetFreeShaderRessourceView()
{
    for (uint32_t heapIndex = 0; heapIndex < ShaderRessourceViewHeaps.size(); ++heapIndex)
    {
        if (ShaderRessourceViewHeaps[heapIndex].UsedHeap != ShaderRessourceViewHeap_t::HeapSize)
            return _GetFreeShaderRessourceViewFromHeap(heapIndex);
    }

    if (_AllocShaderRessourceViewHeap())
        return _GetFreeShaderRessourceViewFromHeap((uint32_t)ShaderRessourceViewHeaps.size()-1);

    return ShaderRessourceView_t{ 0, 0, ShaderRessourceView_t::InvalidId };
}

void DX12_Hook::_ReleaseShaderRessourceView(uint32_t id)
{
    auto& heap = ShaderRessourceViewHeaps[GetHeapIndex(id)];
    heap.HeapBitmap[GetHeapUsedIndex(id)] = false;
    --heap.UsedHeap;
}

ID3D12CommandQueue* DX12_Hook::_FindCommandQueueFromSwapChain(IDXGISwapChain* pSwapChain)
{
    ID3D12CommandQueue* pCommandQueue = nullptr;

    if (CommandQueueOffset == 0 && pCmdQueue != nullptr)
    {
        for (size_t i = 0; i < 1024; ++i)
        {
            if (*reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + i) == pCmdQueue)
            {
                SPDLOG_INFO("Found IDXGISwapChain::ppCommandQueue at offset {}.", i);
                CommandQueueOffset = i;
                break;
            }
        }
    }

    if (CommandQueueOffset != 0)
        pCommandQueue = *reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + CommandQueueOffset);

    return pCommandQueue;
}

void DX12_Hook::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(ingame_overlay::OverlayHookState::Removing);

        ImGui_ImplDX12_Shutdown();
        Windows_Hook::Inst()->ResetRenderState();
        //ImGui::DestroyContext();

        OverlayFrames.clear();

        _ImageResources.clear();
        _ReleaseDX12Ressources();

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX12_Hook::_PrepareForOverlay(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pCommandQueue)
{
    IDXGISwapChain3* pSwapChain3 = nullptr;
    DXGI_SWAP_CHAIN_DESC sc_desc;
    pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
    if (pSwapChain3 == nullptr)
        return;

    pSwapChain3->GetDesc(&sc_desc);

    if (!_Initialized)
    {
        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();
        pDevice = nullptr;
        if (pSwapChain3->GetDevice(IID_PPV_ARGS(&pDevice)) != S_OK)
		{
			pSwapChain3->Release();
            return;
		}

        UINT bufferCount = sc_desc.BufferCount;

        if (!_AllocShaderRessourceViewHeap())
        {
            pDevice->Release();
            pSwapChain3->Release();
            return;
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = bufferCount;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRtvDescHeap)) != S_OK)
            {
                _ReleaseDX12Ressources();
                pSwapChain3->Release();
                return;
            }
        
            SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
            ID3D12CommandAllocator* pCmdAlloc;
            ID3D12Resource* pBackBuffer;

            for (UINT i = 0; i < bufferCount; ++i)
            {
                pCmdAlloc = nullptr;
                pBackBuffer = nullptr;

                if (pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCmdAlloc)) != S_OK || pCmdAlloc == nullptr)
                {
                    OverlayFrames.clear();
                    _ReleaseDX12Ressources();
                    pSwapChain3->Release();
                    return;
                }

                if (i == 0)
                {
                    if (pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCmdAlloc, NULL, IID_PPV_ARGS(&pCmdList)) != S_OK ||
                        pCmdList == nullptr || pCmdList->Close() != S_OK)
                    {
                        OverlayFrames.clear();
                        SafeRelease(pCmdList);
                        pCmdAlloc->Release();
                        _ReleaseDX12Ressources();
                        pSwapChain3->Release();
                        return;
                    }
                }

                if (pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer)) != S_OK || pBackBuffer == nullptr)
                {
                    OverlayFrames.clear();
                    pCmdList->Release();
                    pCmdAlloc->Release();
                    _ReleaseDX12Ressources();
                    pSwapChain3->Release();
                    return;
                }

                pDevice->CreateRenderTargetView(pBackBuffer, NULL, rtvHandle);

                OverlayFrames.emplace_back(rtvHandle, pCmdAlloc, pBackBuffer);
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }

        auto shaderRessourceView = _GetFreeShaderRessourceView();

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX12_Init(pDevice, bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr,
            shaderRessourceView.CpuHandle,
            shaderRessourceView.GpuHandle);
        
        Windows_Hook::Inst()->SetInitialWindowSize(sc_desc.OutputWindow);

        _Initialized = true;
        OverlayHookReady(ingame_overlay::OverlayHookState::Ready);
    }

    if (ImGui_ImplDX12_NewFrame() && Windows_Hook::Inst()->PrepareForOverlay(sc_desc.OutputWindow))
    {
        ImGui::NewFrame();

        OverlayProc();

        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = OverlayFrames[bufferIndex].pBackBuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        OverlayFrames[bufferIndex].pCmdAlloc->Reset();
        pCmdList->Reset(OverlayFrames[bufferIndex].pCmdAlloc, NULL);
        pCmdList->ResourceBarrier(1, &barrier);
        pCmdList->OMSetRenderTargets(1, &OverlayFrames[bufferIndex].RenderTarget, FALSE, NULL);
        //pCmdList->SetDescriptorHeaps(1, ShaderRessourceViewHeapDescriptors);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList, &ShaderRessourceViewHeapDescriptors[0].Heap, (int)ShaderRessourceViewHeapDescriptors.size(), ShaderRessourceViewHeap_t::HeapSize);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        pCmdList->ResourceBarrier(1, &barrier);
        pCmdList->Close();

        pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmdList);
    }

    pSwapChain3->Release();
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    auto inst = DX12_Hook::Inst();

    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue);

    return (_this->*inst->Present)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    auto inst = DX12_Hook::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->ResizeTarget)(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto inst = DX12_Hook::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->ResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyPresent1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    auto inst = DX12_Hook::Inst();
    
    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue);

    return (_this->*inst->Present1)(SyncInterval, Flags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyResizeBuffers1(IDXGISwapChain3* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    auto inst = DX12_Hook::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->ResizeBuffers1)(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

void STDMETHODCALLTYPE DX12_Hook::MyExecuteCommandLists(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    auto inst = DX12_Hook::Inst();
    inst->pCmdQueue = _this;
    (_this->*inst->ExecuteCommandLists)(NumCommandLists, ppCommandLists);
}

DX12_Hook::DX12_Hook():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    CommandQueueOffset(0),
    pCmdQueue(nullptr),
    pDevice(nullptr),
    pCmdList(nullptr),
    pRtvDescHeap(nullptr),
    _ImGuiFontAtlas(nullptr),
    Present(nullptr),
    ResizeBuffers(nullptr),
    ResizeTarget(nullptr),
    ExecuteCommandLists(nullptr),
    Present1(nullptr)
{
    SPDLOG_WARN("DX12 support is experimental, don't complain if it doesn't work as expected.");
}

DX12_Hook::~DX12_Hook()
{
    SPDLOG_INFO("DX12 Hook removed");

    if (_WindowsHooked)
        delete Windows_Hook::Inst();

    if (_Initialized)
    {
        OverlayFrames.clear();

        _ImageResources.clear();        
        ShaderRessourceViewHeaps.clear();
        ShaderRessourceViewHeapDescriptors.clear();
        pRtvDescHeap->Release();

        ImGui_ImplDX12_InvalidateDeviceObjects();
        ImGui::DestroyContext();

        _Initialized = false;
    }

    _inst = nullptr;
}

DX12_Hook* DX12_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new DX12_Hook();

    return _inst;
}

const std::string& DX12_Hook::GetLibraryName() const
{
    return LibraryName;
}

void DX12_Hook::LoadFunctions(
    decltype(Present) PresentFcn,
    decltype(ResizeBuffers) ResizeBuffersFcn,
    decltype(ResizeTarget) ResizeTargetFcn,
    decltype(Present1) Present1Fcn,
    decltype(ResizeBuffers1) ResizeBuffers1Fcn,
    decltype(ExecuteCommandLists) ExecuteCommandListsFcn)
{
    Present = PresentFcn;
    ResizeBuffers = ResizeBuffersFcn;
    ResizeTarget = ResizeTargetFcn;

    Present1 = Present1Fcn;

    ResizeBuffers1 = ResizeBuffers1Fcn;

    ExecuteCommandLists = ExecuteCommandListsFcn;
}

std::weak_ptr<uint64_t> DX12_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    auto shaderRessourceView = _GetFreeShaderRessourceView();
    
    if (shaderRessourceView.Id == ShaderRessourceView_t::InvalidId)
        return std::weak_ptr<uint64_t>{};
    
    HRESULT hr;
    
    D3D12_HEAP_PROPERTIES props;
    memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    D3D12_RESOURCE_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    ID3D12Resource* pTexture = NULL;
    pDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));
    
    UINT uploadPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    UINT uploadSize = height * uploadPitch;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = uploadSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    props.Type = D3D12_HEAP_TYPE_UPLOAD;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    ID3D12Resource* uploadBuffer = NULL;
    hr = pDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
    IM_ASSERT(SUCCEEDED(hr));
    
    void* mapped = NULL;
    D3D12_RANGE range = { 0, uploadSize };
    hr = uploadBuffer->Map(0, &range, &mapped);
    IM_ASSERT(SUCCEEDED(hr));
    for (uint32_t y = 0; y < height; y++)
        memcpy((void*)((uintptr_t)mapped + y * uploadPitch), reinterpret_cast<const uint8_t*>(image_data) + y * width * 4, width * 4);
    uploadBuffer->Unmap(0, &range);
    
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer;
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srcLocation.PlacedFootprint.Footprint.Width = width;
    srcLocation.PlacedFootprint.Footprint.Height = height;
    srcLocation.PlacedFootprint.Footprint.Depth = 1;
    srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;
    
    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = pTexture;
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = pTexture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    ID3D12Fence* fence = NULL;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    IM_ASSERT(SUCCEEDED(hr));
    
    HANDLE event = CreateEvent(0, 0, 0, 0);
    IM_ASSERT(event != NULL);
    
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 1;
    
    ID3D12CommandQueue* cmdQueue = NULL;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    IM_ASSERT(SUCCEEDED(hr));
    
    ID3D12CommandAllocator* cmdAlloc = NULL;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    IM_ASSERT(SUCCEEDED(hr));
    
    ID3D12GraphicsCommandList* cmdList = NULL;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
    IM_ASSERT(SUCCEEDED(hr));
    
    cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
    cmdList->ResourceBarrier(1, &barrier);
    
    hr = cmdList->Close();
    IM_ASSERT(SUCCEEDED(hr));
    
    cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdList);
    hr = cmdQueue->Signal(fence, 1);
    IM_ASSERT(SUCCEEDED(hr));
    
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    
    cmdList->Release();
    cmdAlloc->Release();
    cmdQueue->Release();
    CloseHandle(event);
    fence->Release();
    uploadBuffer->Release();
    
    // Create texture view
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    
    pDevice->CreateShaderResourceView(pTexture, &srvDesc, shaderRessourceView.CpuHandle);
    
    //pSrvDescHeap->Release();
    //pTexture->Release();
    
    static_assert(sizeof(D3D12_GPU_DESCRIPTOR_HANDLE::ptr) == sizeof(ImTextureID), "sizeof(D3D12_GPU_DESCRIPTOR_HANDLE::ptr) must match sizeof(ImTextureID)");
    struct Texture_t{
        ImTextureID GpuHandle; // This must be the first member, ImGui will use the content of the pointer as a D3D12_GPU_DESCRIPTOR_HANDLE::ptr
        ID3D12Resource* pTexture;
        uint32_t Id;
    };
    
    Texture_t* pTextureData = new Texture_t;
    pTextureData->GpuHandle = shaderRessourceView.GpuHandle.ptr;
    pTextureData->pTexture = pTexture;
    pTextureData->Id = shaderRessourceView.Id;
    
    auto image = std::shared_ptr<uint64_t>((uint64_t*)pTextureData, [this](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            auto pTextureData = reinterpret_cast<Texture_t*>(handle);
            pTextureData->pTexture->Release();
            _ReleaseShaderRessourceView(pTextureData->Id);

            delete pTextureData;
        }
    });

    _ImageResources.emplace(image);

    return image;
}

void DX12_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}