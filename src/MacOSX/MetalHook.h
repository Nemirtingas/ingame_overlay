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

#include "../InternalIncludes.h"

#include <Metal/Metal.h>
#include <Metal/MTLDrawable.h>
#include <MetalKit/MetalKit.h>
#include <objc/runtime.h>

namespace InGameOverlay {

class MetalHook_t :
    public InGameOverlay::RendererHookInternal_t,
    public BaseHook_t
{
private:
    static MetalHook_t* _Instance;

    struct RenderPass_t
    {
        MTLRenderPassDescriptor* Descriptor;
        id<MTLCommandBuffer> CommandBuffer;

        RenderPass_t(
            MTLRenderPassDescriptor* descriptor,
            id<MTLCommandBuffer> commandBuffer)
            : Descriptor([descriptor retain]),
            CommandBuffer([commandBuffer retain])
        {
        }

        ~RenderPass_t()
        {
            [Descriptor release] ;
            [CommandBuffer release] ;

            Descriptor = nil;
            CommandBuffer = nil;
        }

        RenderPass_t(const RenderPass_t&) = delete;
        RenderPass_t& operator=(const RenderPass_t&) = delete;

        RenderPass_t(RenderPass_t&& other) noexcept
            : Descriptor(other.Descriptor),
            CommandBuffer(other.CommandBuffer)
        {
            other.Descriptor = nil;
            other.CommandBuffer = nil;
        }

        RenderPass_t& operator=(RenderPass_t&& other) noexcept
        {
            if (this != &other)
            {
                [Descriptor release] ;
                [CommandBuffer release] ;

                Descriptor = other.Descriptor;
                CommandBuffer = other.CommandBuffer;

                other.Descriptor = nil;
                other.CommandBuffer = nil;
            }

            return *this;
        }
    };

    // Variables
    bool _Hooked;
    bool _NSViewHooked;
    bool _Initialized;
    std::set<std::shared_ptr<RendererTexture_t>> _ImageResources;
    std::vector<RendererTextureLoadParameter_t> _ImageResourcesToLoad;
    std::vector<RendererTextureReleaseParameter_t> _ImageResourcesToRelease;
    id<MTLDevice> _MetalDevice;
    std::vector<RenderPass_t> _RenderPass;

    void* _ImGuiFontAtlas;

    // Functions
    MetalHook_t();

    void _ResetRenderState();
    void _PrepareForOverlay(id<MTLCommandBuffer> commandBuffer, MTLRenderPassDescriptor* renderPassDescriptor, id <MTLRenderCommandEncoder>);
    void _LoadResources();
    void _ReleaseResources();
    void _HandleScreenshot();

    // Hook to render functions
    Method _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod;
    Method _MTLCommandBufferPresentDrawableMethod;

    id<MTLRenderCommandEncoder>(*_MTLCommandBufferRenderCommandEncoderWithDescriptor)(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor);
    void (*_MTLCommandBufferPresentDrawable)(id<MTLCommandBuffer> self, SEL sel, id<MTLDrawable> drawable);

public:
    std::string LibraryName;

    static id<MTLRenderCommandEncoder> MyMTLCommandBufferRenderCommandEncoderWithDescriptor(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor);
    static void MyMTLCommandBufferPresentDrawable(id<MTLCommandBuffer> self, SEL sel, id<MTLDrawable> drawable);

    virtual ~MetalHook_t();

    virtual bool StartHook(std::function<void()> keyCombinationCcallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static MetalHook_t* Inst();
    virtual const char* GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(Method MTLCommandBufferRenderCommandEncoderWithDescriptor, Method MTLCommandBufferPresentDrawable);

    virtual std::weak_ptr<RendererTexture_t> AllocImageResource();
    virtual void LoadImageResource(RendererTextureLoadParameter_t& loadParameter);
    virtual void ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource);
};

}// namespace InGameOverlay
