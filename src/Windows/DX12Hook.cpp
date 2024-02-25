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
#include <backends/imgui_impl_dx12.h>

namespace InGameOverlay {

DX12Hook_t* DX12Hook_t::_Instance = nullptr;

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

bool DX12Hook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_IDXGISwapChainPresent == nullptr || _IDXGISwapChainResizeTarget == nullptr || _IDXGISwapChainResizeBuffers == nullptr || _ID3D12CommandQueueExecuteCommandLists == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 12: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        SPDLOG_INFO("Hooked DirectX 12");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainPresent                , &DX12Hook_t::_MyIDXGISwapChainPresent),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeTarget           , &DX12Hook_t::_MyIDXGISwapChainResizeBuffers),
            std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChainResizeBuffers          , &DX12Hook_t::_MyIDXGISwapChainResizeTarget),
            std::make_pair<void**, void*>(&(PVOID&)_ID3D12CommandQueueExecuteCommandLists, &DX12Hook_t::_MyID3D12CommandQueueExecuteCommandLists)
        );
        if (_IDXGISwapChain1Present1 != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChain1Present1, &DX12Hook_t::_MyIDXGISwapChain1Present1)
            );
        }
        if (_IDXGISwapChain3ResizeBuffers1 != nullptr)
        {
            HookFuncs(
                std::make_pair<void**, void*>(&(PVOID&)_IDXGISwapChain3ResizeBuffers1, &DX12Hook_t::_MyIDXGISwapChain3ResizeBuffers1)
            );
        }
        EndHook();
    }
    return true;
}

void DX12Hook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideAppInputs(hide);
    }
}

void DX12Hook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
    }
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
    desc.NumDescriptors = ShaderRessourceViewHeap_t::HeapSize;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)) != S_OK)
        return false;

    _ShaderRessourceViewHeapDescriptors.emplace_back(pHeap);
    _ShaderRessourceViewHeaps.emplace_back();
    return true;
}

DX12Hook_t::ShaderRessourceView_t DX12Hook_t::_GetFreeShaderRessourceViewFromHeap(uint32_t heapIndex)
{
    ShaderRessourceView_t result{};
    UINT inc = _Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto& heap = _ShaderRessourceViewHeaps[heapIndex];
    for (uint32_t usedIndex = 0; usedIndex < heap.HeapBitmap.size(); ++usedIndex)
    {
        if (!heap.HeapBitmap[usedIndex])
        {
            heap.HeapBitmap[usedIndex] = true;
            ++heap.UsedHeap;
            result.CpuHandle.ptr = _ShaderRessourceViewHeapDescriptors[heapIndex].Heap->GetCPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.GpuHandle.ptr = _ShaderRessourceViewHeapDescriptors[heapIndex].Heap->GetGPUDescriptorHandleForHeapStart().ptr + inc * usedIndex;
            result.Id = MakeHeapId(heapIndex, usedIndex);
            break;
        }
    }

    return result;
}

DX12Hook_t::ShaderRessourceView_t DX12Hook_t::_GetFreeShaderRessourceView()
{
    for (uint32_t heapIndex = 0; heapIndex < _ShaderRessourceViewHeaps.size(); ++heapIndex)
    {
        if (_ShaderRessourceViewHeaps[heapIndex].UsedHeap != ShaderRessourceViewHeap_t::HeapSize)
            return _GetFreeShaderRessourceViewFromHeap(heapIndex);
    }

    if (_AllocShaderRessourceViewHeap())
        return _GetFreeShaderRessourceViewFromHeap((uint32_t)_ShaderRessourceViewHeaps.size()-1);

    return ShaderRessourceView_t{ 0, 0, ShaderRessourceView_t::InvalidId };
}

void DX12Hook_t::_ReleaseShaderRessourceView(uint32_t id)
{
    auto& heap = _ShaderRessourceViewHeaps[GetHeapIndex(id)];
    heap.HeapBitmap[GetHeapUsedIndex(id)] = false;
    --heap.UsedHeap;
}

ID3D12CommandQueue* DX12Hook_t::_FindCommandQueueFromSwapChain(IDXGISwapChain* pSwapChain)
{
    ID3D12CommandQueue* pCommandQueue = nullptr;

    if (_CommandQueueOffset == 0 && _CommandQueue != nullptr)
    {
        for (size_t i = 0; i < 1024; ++i)
        {
            if (*reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + i) == _CommandQueue)
            {
                SPDLOG_INFO("Found IDXGISwapChain::ppCommandQueue at offset {}.", i);
                _CommandQueueOffset = i;
                break;
            }
        }
    }

    if (_CommandQueueOffset != 0)
        pCommandQueue = *reinterpret_cast<ID3D12CommandQueue**>(reinterpret_cast<uintptr_t>(pSwapChain) + _CommandQueueOffset);

    return pCommandQueue;
}

void DX12Hook_t::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(InGameOverlay::OverlayHookState::Removing);

        ImGui_ImplDX12_Shutdown();
        WindowsHook_t::Inst()->ResetRenderState();
        //ImGui::DestroyContext();

        _OverlayFrames.clear();

        _ImageResources.clear();
        _ShaderRessourceViewHeaps.clear();
        _ShaderRessourceViewHeapDescriptors.clear();
        SafeRelease(_RenderTargetViewDescriptorHeap);
        SafeRelease(_Device);

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX12Hook_t::_PrepareForOverlay(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pCommandQueue)
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
        _Device = nullptr;
        if (pSwapChain3->GetDevice(IID_PPV_ARGS(&_Device)) != S_OK)
		{
			pSwapChain3->Release();
            return;
		}

        UINT bufferCount = sc_desc.BufferCount;

        if (!_AllocShaderRessourceViewHeap())
        {
            _Device->Release();
            pSwapChain3->Release();
            return;
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = bufferCount;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_RenderTargetViewDescriptorHeap)) != S_OK)
            {
                _ShaderRessourceViewHeaps.clear();
                _ShaderRessourceViewHeapDescriptors.clear();
                _Device->Release();
                pSwapChain3->Release();
                return;
            }
        
            SIZE_T rtvDescriptorSize = _Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _RenderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            ID3D12CommandAllocator* pCmdAlloc;
            ID3D12Resource* pBackBuffer;

            for (UINT i = 0; i < bufferCount; ++i)
            {
                pCmdAlloc = nullptr;
                pBackBuffer = nullptr;

                if (_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCmdAlloc)) != S_OK || pCmdAlloc == nullptr)
                {
                    _OverlayFrames.clear();
                    _ShaderRessourceViewHeaps.clear();
                    _ShaderRessourceViewHeapDescriptors.clear();
                    _RenderTargetViewDescriptorHeap->Release();
                    _Device->Release();
                    pSwapChain3->Release();
                    return;
                }

                if (i == 0)
                {
                    if (_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCmdAlloc, NULL, IID_PPV_ARGS(&_CommandList)) != S_OK ||
                        _CommandList == nullptr || _CommandList->Close() != S_OK)
                    {
                        _OverlayFrames.clear();
                        SafeRelease(_CommandList);
                        pCmdAlloc->Release();
                        _ShaderRessourceViewHeaps.clear();
                        _ShaderRessourceViewHeapDescriptors.clear();
                        _RenderTargetViewDescriptorHeap->Release();
                        _Device->Release();
                        pSwapChain3->Release();
                        return;
                    }
                }

                if (pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer)) != S_OK || pBackBuffer == nullptr)
                {
                    _OverlayFrames.clear();
                    _CommandList->Release();
                    pCmdAlloc->Release();
                    _ShaderRessourceViewHeaps.clear();
                    _ShaderRessourceViewHeapDescriptors.clear();
                    _RenderTargetViewDescriptorHeap->Release();
                    _Device->Release();
                    pSwapChain3->Release();
                    return;
                }

                _Device->CreateRenderTargetView(pBackBuffer, NULL, rtvHandle);

                _OverlayFrames.emplace_back(rtvHandle, pCmdAlloc, pBackBuffer);
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }

        auto shaderRessourceView = _GetFreeShaderRessourceView();

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplDX12_Init(_Device, bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr,
            shaderRessourceView.CpuHandle,
            shaderRessourceView.GpuHandle);
        
        WindowsHook_t::Inst()->SetInitialWindowSize(sc_desc.OutputWindow);

        _Initialized = true;
        OverlayHookReady(InGameOverlay::OverlayHookState::Ready);
    }

    if (ImGui_ImplDX12_NewFrame() && WindowsHook_t::Inst()->PrepareForOverlay(sc_desc.OutputWindow))
    {
        ImGui::NewFrame();

        OverlayProc();

        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = _OverlayFrames[bufferIndex].pBackBuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        _OverlayFrames[bufferIndex].pCmdAlloc->Reset();
        _CommandList->Reset(_OverlayFrames[bufferIndex].pCmdAlloc, NULL);
        _CommandList->ResourceBarrier(1, &barrier);
        _CommandList->OMSetRenderTargets(1, &_OverlayFrames[bufferIndex].RenderTarget, FALSE, NULL);
        //pCmdList->SetDescriptorHeaps(1, ShaderRessourceViewHeapDescriptors);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), _CommandList, &_ShaderRessourceViewHeapDescriptors[0].Heap, (int)_ShaderRessourceViewHeapDescriptors.size(), ShaderRessourceViewHeap_t::HeapSize);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        _CommandList->ResourceBarrier(1, &barrier);
        _CommandList->Close();

        pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&_CommandList);
    }

    pSwapChain3->Release();
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    auto inst = DX12Hook_t::Inst();

    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue);

    return (_this->*inst->_IDXGISwapChainPresent)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    auto inst = DX12Hook_t::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->_IDXGISwapChainResizeTarget)(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto inst = DX12Hook_t::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->_IDXGISwapChainResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    auto inst = DX12Hook_t::Inst();
    
    ID3D12CommandQueue* pCommandQueue = inst->_FindCommandQueueFromSwapChain(_this);
    if (pCommandQueue != nullptr)
        inst->_PrepareForOverlay(_this, pCommandQueue);

    return (_this->*inst->_IDXGISwapChain1Present1)(SyncInterval, Flags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE DX12Hook_t::_MyIDXGISwapChain3ResizeBuffers1(IDXGISwapChain3* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    auto inst = DX12Hook_t::Inst();
    inst->_ResetRenderState();
    return (_this->*inst->_IDXGISwapChain3ResizeBuffers1)(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

void STDMETHODCALLTYPE DX12Hook_t::_MyID3D12CommandQueueExecuteCommandLists(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    auto inst = DX12Hook_t::Inst();
    inst->_CommandQueue = _this;
    (_this->*inst->_ID3D12CommandQueueExecuteCommandLists)(NumCommandLists, ppCommandLists);
}

DX12Hook_t::DX12Hook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    _CommandQueueOffset(0),
    _CommandQueue(nullptr),
    _Device(nullptr),
    _CommandList(nullptr),
    _RenderTargetViewDescriptorHeap(nullptr),
    _ImGuiFontAtlas(nullptr),
    _IDXGISwapChainPresent(nullptr),
    _IDXGISwapChainResizeBuffers(nullptr),
    _IDXGISwapChainResizeTarget(nullptr),
    _ID3D12CommandQueueExecuteCommandLists(nullptr),
    _IDXGISwapChain1Present1(nullptr)
{
    SPDLOG_WARN("DX12 support is experimental, don't complain if it doesn't work as expected.");
}

DX12Hook_t::~DX12Hook_t()
{
    SPDLOG_INFO("DX12 Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    if (_Initialized)
    {
        _OverlayFrames.clear();

        _ImageResources.clear();        
        _ShaderRessourceViewHeaps.clear();
        _ShaderRessourceViewHeapDescriptors.clear();
        _RenderTargetViewDescriptorHeap->Release();

        ImGui_ImplDX12_InvalidateDeviceObjects();
        ImGui::DestroyContext();

        _Initialized = false;
    }

    _Instance = nullptr;
}

DX12Hook_t* DX12Hook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new DX12Hook_t();

    return _Instance;
}

const std::string& DX12Hook_t::GetLibraryName() const
{
    return LibraryName;
}

void DX12Hook_t::LoadFunctions(
    decltype(_IDXGISwapChainPresent) PresentFcn,
    decltype(_IDXGISwapChainResizeBuffers) ResizeBuffersFcn,
    decltype(_IDXGISwapChainResizeTarget) ResizeTargetFcn,
    decltype(_IDXGISwapChain1Present1) Present1Fcn,
    decltype(_IDXGISwapChain3ResizeBuffers1) ResizeBuffers1Fcn,
    decltype(_ID3D12CommandQueueExecuteCommandLists) ExecuteCommandListsFcn)
{
    _IDXGISwapChainPresent = PresentFcn;
    _IDXGISwapChainResizeBuffers = ResizeBuffersFcn;
    _IDXGISwapChainResizeTarget = ResizeTargetFcn;

    _IDXGISwapChain1Present1 = Present1Fcn;

    _IDXGISwapChain3ResizeBuffers1 = ResizeBuffers1Fcn;

    _ID3D12CommandQueueExecuteCommandLists = ExecuteCommandListsFcn;
}

std::weak_ptr<uint64_t> DX12Hook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
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
    _Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
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
    hr = _Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
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
    hr = _Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    IM_ASSERT(SUCCEEDED(hr));
    
    HANDLE event = CreateEvent(0, 0, 0, 0);
    IM_ASSERT(event != NULL);
    
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 1;
    
    ID3D12CommandQueue* cmdQueue = NULL;
    hr = _Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    IM_ASSERT(SUCCEEDED(hr));
    
    ID3D12CommandAllocator* cmdAlloc = NULL;
    hr = _Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    IM_ASSERT(SUCCEEDED(hr));
    
    ID3D12GraphicsCommandList* cmdList = NULL;
    hr = _Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
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
    
    _Device->CreateShaderResourceView(pTexture, &srvDesc, shaderRessourceView.CpuHandle);
    
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

void DX12Hook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}

}// namespace InGameOverlay