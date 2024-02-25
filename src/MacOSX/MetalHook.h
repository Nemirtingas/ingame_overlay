/*
 * Copyright (C) 2019-2020 Nemirtingas
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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>

#include <objc/runtime.h>

namespace InGameOverlay {

class MetalHook_t :
    public RendererHook_t,
    public BaseHook_t
{
public:
    static constexpr const char *DLL_NAME = "Metal";

private:
    static MetalHook_t* _Instance;

    struct RenderPass_t
    {
        MTLRenderPassDescriptor* Descriptor;
        id<MTLCommandBuffer> CommandBuffer;
        id<MTLRenderCommandEncoder> Encoder;
    };
    
    // Variables
    bool _Hooked;
    bool _NSViewHooked;
    bool _Initialized;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    id<MTLDevice> _MetalDevice;
    std::vector<RenderPass_t> _RenderPass;
    
    void* _ImGuiFontAtlas;

    // Functions
    MetalHook_t();

    void _ResetRenderState();
    void _PrepareForOverlay(RenderPass_t& renderPass);

    // Hook to render functions
    Method _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod;
    Method _MTLRenderCommandEncoderEndEncodingMethod;

    id<MTLRenderCommandEncoder> (*_MTLCommandBufferRenderCommandEncoderWithDescriptor)(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor);
    void (*_MTLRenderCommandEncoderEndEncoding)(id<MTLRenderCommandEncoder> self, SEL sel);

public:
    std::string LibraryName;

    static id<MTLRenderCommandEncoder> MyMTLCommandBufferRenderCommandEncoderWithDescriptor(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor);
    static void MyMTLCommandEncoderEndEncoding(id<MTLRenderCommandEncoder> self, SEL sel);

    virtual ~MetalHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static MetalHook_t* Inst();
    virtual const std::string& GetLibraryName() const;
    void LoadFunctions(Method MTLCommandBufferRenderCommandEncoderWithDescriptor, Method RenderCommandEncoderEndEncoding);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay