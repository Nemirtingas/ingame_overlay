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

#pragma once

#include "../RendererHookInternal.h"

#include <d3d12.h>
#include <dxgi1_4.h>

namespace InGameOverlay {

class DX12Hook_t : 
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
private:
    static DX12Hook_t* _Instance;

    struct ID3D12DescriptorHeapWrapper_t
    {
        ID3D12DescriptorHeap* Heap;

        inline ID3D12DescriptorHeapWrapper_t(ID3D12DescriptorHeap* heap):Heap(heap) {}

        inline ID3D12DescriptorHeapWrapper_t(ID3D12DescriptorHeapWrapper_t&& other) noexcept
        {
            Heap = other.Heap;
            other.Heap = nullptr;
        }

        inline ID3D12DescriptorHeapWrapper_t& operator=(ID3D12DescriptorHeapWrapper_t&& other) noexcept
        {
            Heap = other.Heap;
            other.Heap = nullptr;
            return *this;
        }

        inline ID3D12DescriptorHeapWrapper_t(ID3D12DescriptorHeapWrapper_t const&) = delete;
        inline ID3D12DescriptorHeapWrapper_t& operator=(ID3D12DescriptorHeapWrapper_t const&) = delete;

        inline ~ID3D12DescriptorHeapWrapper_t() { if (Heap != nullptr) Heap->Release(); }
    };

    struct ShaderResourceViewHeap_t
    {
        static constexpr uint32_t HeapSize = 1024;
        std::array<bool, HeapSize> HeapBitmap;
        uint32_t UsedHeap;

        inline ShaderResourceViewHeap_t()
            : HeapBitmap{}, UsedHeap{}
        {}
    };

    struct ShaderRessourceView_t
    {
        static constexpr uint32_t InvalidId = 0xffffffff;

        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
        uint32_t Id;
    };

    struct DX12Frame_t
    {
        D3D12_CPU_DESCRIPTOR_HANDLE RenderTarget = {};
        ID3D12CommandAllocator* CommandAllocator = nullptr;
        ID3D12GraphicsCommandList* CommandList = nullptr;
        ID3D12Resource* BackBuffer = nullptr;

        inline void Reset()
        {
            CommandAllocator = nullptr;
            CommandList = nullptr;
            BackBuffer = nullptr;
        }

        DX12Frame_t() = default;

        DX12Frame_t(DX12Frame_t const&) = delete;
        DX12Frame_t& operator=(DX12Frame_t const&) = delete;

        DX12Frame_t(DX12Frame_t&& other) noexcept:
            RenderTarget(other.RenderTarget), CommandAllocator(other.CommandAllocator), BackBuffer(other.BackBuffer)
        {
            other.Reset();
        }

        DX12Frame_t& operator=(DX12Frame_t&& other) noexcept
        {
            RenderTarget = other.RenderTarget;
            CommandAllocator = other.CommandAllocator;
            CommandList = other.CommandList;
            BackBuffer = other.BackBuffer;
            other.Reset();

            return *this;
        }

        ~DX12Frame_t()
        {
            if (CommandAllocator != nullptr) CommandAllocator->Release();
            if (CommandList != nullptr) CommandList->Release();
            if (BackBuffer != nullptr) BackBuffer->Release();
        }
    };

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    uint32_t _DeviceReleasing;

    int _CommandQueueOffsetRetries;
    size_t _CommandQueueOffset;
    ID3D12CommandQueue* _CommandQueue;
    ID3D12Device* _Device;
    ULONG _HookDeviceRefCount;
    OverlayHookState _HookState;
    std::vector<DX12Frame_t> _OverlayFrames;
    std::vector<ID3D12DescriptorHeapWrapper_t> _ShaderResourceViewHeapDescriptors;
    std::vector<ShaderResourceViewHeap_t> _ShaderResourceViewHeaps;
    // Render Target View heap
    ID3D12DescriptorHeap* _RenderTargetViewDescriptorHeap;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    DX12Hook_t();
    
    bool _AllocShaderRessourceViewHeap();
    ShaderRessourceView_t _GetFreeShaderRessourceViewFromHeap(uint32_t heapIndex);
    ShaderRessourceView_t _GetFreeShaderRessourceView();
    void _ReleaseShaderRessourceView(uint32_t id);

    ID3D12CommandQueue* _FindCommandQueueFromSwapChain(IDXGISwapChain* pSwapChain);

    void _UpdateHookDeviceRefCount();
    bool _CreateRenderTargets(IDXGISwapChain* pSwapChain);
    void _DestroyRenderTargets();
    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pCommandQueue, UINT flags);
    void _HandleScreenshot(DX12Frame_t& frame);
    bool _CaptureScreenshot(DX12Frame_t& frame, ScreenshotData_t& outData);

    // Hook to render functions
    decltype(&ID3D12Device::Release)                   _ID3D12DeviceRelease;
    decltype(&IDXGISwapChain::Present)                 _IDXGISwapChainPresent;
    decltype(&IDXGISwapChain::ResizeBuffers)           _IDXGISwapChainResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget)            _IDXGISwapChainResizeTarget;
    decltype(&IDXGISwapChain1::Present1)               _IDXGISwapChain1Present1;
    decltype(&IDXGISwapChain3::ResizeBuffers1)         _IDXGISwapChain3ResizeBuffers1;
    decltype(&ID3D12CommandQueue::ExecuteCommandLists) _ID3D12CommandQueueExecuteCommandLists;

    static ULONG   STDMETHODCALLTYPE _MyID3D12DeviceRelease(IUnknown* _this);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChainResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChain1Present1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
    static HRESULT STDMETHODCALLTYPE _MyIDXGISwapChain3ResizeBuffers1(IDXGISwapChain3* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);
    static void    STDMETHODCALLTYPE _MyID3D12CommandQueueExecuteCommandLists(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

public:
    std::string LibraryName;

    virtual ~DX12Hook_t();

    virtual bool StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static DX12Hook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;

    void LoadFunctions(
        decltype(_ID3D12DeviceRelease) releaseFcn,
        decltype(_IDXGISwapChainPresent) presentFcn,
        decltype(_IDXGISwapChainResizeBuffers) resizeBuffersFcn,
        decltype(_IDXGISwapChainResizeTarget) resizeTargetFcn,
        decltype(_IDXGISwapChain1Present1) present1Fcn1,
        decltype(_IDXGISwapChain3ResizeBuffers1) resizeBuffers1Fcn,
        decltype(_ID3D12CommandQueueExecuteCommandLists) xecuteCommandListsFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay