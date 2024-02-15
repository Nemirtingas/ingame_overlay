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

#include <ingame_overlay/Renderer_Hook.h>

#include "../internal_includes.h"

#include <d3d12.h>
#include <dxgi1_4.h>

class DX12_Hook : 
    public ingame_overlay::Renderer_Hook,
    public Base_Hook
{
public:
    static constexpr const char *DLL_NAME = "d3d12.dll";

private:
    static DX12_Hook* _inst;

    struct ID3D12DescriptorHeapWrapper_t
    {
        ID3D12DescriptorHeap* Heap;

        inline ID3D12DescriptorHeapWrapper_t(ID3D12DescriptorHeap* heap):Heap(heap) {}

        inline ~ID3D12DescriptorHeapWrapper_t() { Heap->Release(); }
    };

    struct ShaderRessourceViewHeap_t
    {
        static constexpr uint32_t HeapSize = 1024;
        std::array<bool, HeapSize> HeapBitmap;
        uint32_t UsedHeap;

        inline ShaderRessourceViewHeap_t()
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
        ID3D12CommandAllocator* pCmdAlloc = nullptr;
        ID3D12Resource* pBackBuffer = nullptr;

        inline void Reset()
        {
            pCmdAlloc = nullptr;
            pBackBuffer = nullptr;
        }

        DX12Frame_t(DX12Frame_t const&) = delete;
        DX12Frame_t& operator=(DX12Frame_t const&) = delete;

        DX12Frame_t(D3D12_CPU_DESCRIPTOR_HANDLE RenderTarget, ID3D12CommandAllocator* pCmdAlloc, ID3D12Resource* pBackBuffer):
            RenderTarget(RenderTarget), pCmdAlloc(pCmdAlloc), pBackBuffer(pBackBuffer)
        {}

        DX12Frame_t(DX12Frame_t&& other) noexcept:
            RenderTarget(other.RenderTarget), pCmdAlloc(other.pCmdAlloc), pBackBuffer(other.pBackBuffer)
        {
            other.Reset();
        }

        DX12Frame_t& operator=(DX12Frame_t&& other) noexcept
        {
            DX12Frame_t tmp(std::move(other));
            RenderTarget = tmp.RenderTarget;
            pCmdAlloc = tmp.pCmdAlloc;
            pBackBuffer = tmp.pBackBuffer;
            tmp.Reset();

            return *this;
        }

        ~DX12Frame_t()
        {
            if (pCmdAlloc != nullptr) pCmdAlloc->Release();
            if (pBackBuffer != nullptr) pBackBuffer->Release();
        }
    };

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;

    size_t CommandQueueOffset;
    ID3D12CommandQueue* pCmdQueue;
    ID3D12Device* pDevice;
    std::vector<DX12Frame_t> OverlayFrames;
    std::vector<ID3D12DescriptorHeapWrapper_t> ShaderRessourceViewHeapDescriptors;
    std::vector<ShaderRessourceViewHeap_t> ShaderRessourceViewHeaps;
    ID3D12GraphicsCommandList* pCmdList;
    // Render Target View heap
    ID3D12DescriptorHeap* pRtvDescHeap;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    DX12_Hook();
    
    bool _AllocShaderRessourceViewHeap();
    ShaderRessourceView_t _GetFreeShaderRessourceViewFromHeap(uint32_t heapIndex);
    ShaderRessourceView_t _GetFreeShaderRessourceView();
    void _ReleaseShaderRessourceView(uint32_t id);

    ID3D12CommandQueue* _FindCommandQueueFromSwapChain(IDXGISwapChain* pSwapChain);

    void _ResetRenderState();
    void _PrepareForOverlay(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pCommandQueue);

    // Hook to render functions
    static HRESULT STDMETHODCALLTYPE MyPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT STDMETHODCALLTYPE MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE MyPresent1(IDXGISwapChain1* _this, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
    static HRESULT STDMETHODCALLTYPE MyResizeBuffers1(IDXGISwapChain3* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);
    static void STDMETHODCALLTYPE MyExecuteCommandLists(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

    decltype(&IDXGISwapChain::Present)       Present;
    decltype(&IDXGISwapChain::ResizeBuffers) ResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget)  ResizeTarget;
    decltype(&IDXGISwapChain1::Present1)     Present1;
    decltype(&IDXGISwapChain3::ResizeBuffers1) ResizeBuffers1;
    decltype(&ID3D12CommandQueue::ExecuteCommandLists) ExecuteCommandLists;

public:
    std::string LibraryName;

    virtual ~DX12_Hook();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static DX12_Hook* Inst();
    virtual std::string GetLibraryName() const;

    void LoadFunctions(
        decltype(Present) PresentFcn,
        decltype(ResizeBuffers) ResizeBuffersFcn,
        decltype(ResizeTarget) ResizeTargetFcn,
        decltype(Present1) Present1Fcn1,
        decltype(ResizeBuffers1) ResizeBuffers1Fcn,
        decltype(ExecuteCommandLists) ExecuteCommandListsFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
