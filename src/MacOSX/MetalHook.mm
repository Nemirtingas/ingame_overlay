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

#include "MetalHook.h"
#include "NSViewHook.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_metal.h>

namespace InGameOverlay {

MetalHook_t* MetalHook_t::_Instance = nullptr;

bool MetalHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod == nil || _MTLCommandBufferPresentDrawableMethod == nil)
        {
            INGAMEOVERLAY_WARN("Failed to hook Metal: Rendering functions missing.");
            return false;
        }

        if (!NSViewHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;
        
        _NSViewHooked = true;

        _MTLCommandBufferRenderCommandEncoderWithDescriptor = (decltype(_MTLCommandBufferRenderCommandEncoderWithDescriptor))method_setImplementation(_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod, (IMP)&MyMTLCommandBufferRenderCommandEncoderWithDescriptor);
        _MTLCommandBufferPresentDrawable = (decltype(_MTLCommandBufferPresentDrawable))method_setImplementation(_MTLCommandBufferPresentDrawableMethod,                                                                                  (IMP)&MyMTLCommandBufferPresentDrawable);

        INGAMEOVERLAY_INFO("Hooked Metal");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void MetalHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        NSViewHook_t::Inst()->HideAppInputs(hide);
    }
}

void MetalHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        NSViewHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool MetalHook_t::IsStarted()
{
    return _Hooked;
}

void MetalHook_t::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(InGameOverlay::OverlayHookState::Removing);

        ImGui_ImplMetal_Shutdown();
        //NSViewHook_t::Inst()->_ResetRenderState();
        //ImGui::DestroyContext();

        _ImageResources.clear();

        _MetalDevice = nil;
        
        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void MetalHook_t::_PrepareForOverlay(id<MTLCommandBuffer> commandBuffer, MTLRenderPassDescriptor* renderPassDescriptor, id<MTLRenderCommandEncoder> renderEncoder)
{
    if (!_Initialized)
    {
        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));
        
        _MetalDevice = [commandBuffer device];

        ImGui_ImplMetal_Init(_MetalDevice);
        
        _Initialized = true;
        OverlayHookReady(InGameOverlay::OverlayHookState::Ready);
    }
    
    if (NSViewHook_t::Inst()->PrepareForOverlay() && ImGui_ImplMetal_NewFrame(renderPassDescriptor))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot();

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

        ImGui::Render();

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot();
    }
}

void MetalHook_t::_LoadResources()
{

}

void MetalHook_t::_ReleaseResources()
{

}

void MetalHook_t::_HandleScreenshot()
{
    _SendScreenshot(nullptr);
}

id<MTLRenderCommandEncoder> MetalHook_t::MyMTLCommandBufferRenderCommandEncoderWithDescriptor(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor)
{
    MetalHook_t* inst = MetalHook_t::Inst();
    id<MTLRenderCommandEncoder> encoder = inst->_MTLCommandBufferRenderCommandEncoderWithDescriptor(self, sel, descriptor);
    
    auto found = false;
    for (auto& renderPass : inst->_RenderPass)
    {
        if (renderPass.CommandBuffer == self)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        inst->_RenderPass.emplace_back(descriptor, self);
    }
    
    return encoder;
}

void MetalHook_t::MyMTLCommandBufferPresentDrawable(id<MTLCommandBuffer> self, SEL sel, id<MTLDrawable> drawable)
{
    MetalHook_t* inst = MetalHook_t::Inst();
    
    for (auto& renderPass : inst->_RenderPass)
    {
        if (renderPass.CommandBuffer == self)
        {
            if (renderPass.CommandBuffer != self)
                continue;
            
            if (renderPass.Descriptor.colorAttachments[0].texture == nil)
                continue;
            
            MTLRenderPassDescriptor* overlayDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
            
            overlayDescriptor.colorAttachments[0].texture =
            renderPass.Descriptor.colorAttachments[0].texture;
            //[(id<CAMetalDrawable>)drawable texture];
            
            overlayDescriptor.colorAttachments[0].loadAction =
            MTLLoadActionLoad;
            
            overlayDescriptor.colorAttachments[0].storeAction =
            MTLStoreActionStore;
            
            id<MTLRenderCommandEncoder> renderEncoder =
            inst->_MTLCommandBufferRenderCommandEncoderWithDescriptor(
                                                                      self,
                                                                      @selector(renderCommandEncoderWithDescriptor:),
                                                                      overlayDescriptor);
            
            inst->_PrepareForOverlay(
                                     self,
                                     overlayDescriptor,
                                     renderEncoder);
            
            [renderEncoder endEncoding];
            
            break;
        }
    }
    
    inst->_RenderPass.clear();
    
    inst->_MTLCommandBufferPresentDrawable(self, sel, drawable);
}

MetalHook_t::MetalHook_t():
    _Initialized(false),
    _Hooked(false),
    _ImGuiFontAtlas(nullptr),
    _MetalDevice(nil),
    _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod(nil),
    _MTLCommandBufferPresentDrawableMethod(nil),
    _MTLCommandBufferRenderCommandEncoderWithDescriptor(nullptr),
    _MTLCommandBufferPresentDrawable(nullptr)
{
    
}

MetalHook_t::~MetalHook_t()
{
    INGAMEOVERLAY_INFO("Metal Hook removed");

    if (_NSViewHooked)
        delete NSViewHook_t::Inst();

    if (_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod != nil && _MTLCommandBufferRenderCommandEncoderWithDescriptor != nullptr)
    {
        method_setImplementation(_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod, (IMP)_MTLCommandBufferRenderCommandEncoderWithDescriptor);
        _MTLCommandBufferRenderCommandEncoderWithDescriptor = nullptr;
    }
    if (_MTLCommandBufferPresentDrawableMethod != nil && _MTLCommandBufferPresentDrawable != nullptr)
    {
        method_setImplementation(_MTLCommandBufferPresentDrawableMethod, (IMP)_MTLCommandBufferPresentDrawable);
        _MTLCommandBufferPresentDrawable = nullptr;
    }

    if (_Initialized)
    {
        ImGui_ImplMetal_Shutdown();
        ImGui::DestroyContext();
        _MetalDevice = nil;
    }

    _Instance->UnhookAll();
    _Instance = nullptr;
}

MetalHook_t* MetalHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new MetalHook_t;

    return _Instance;
}

const char* MetalHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t MetalHook_t::GetRendererHookType() const
{
    return RendererHookType_t::Metal;
}

void MetalHook_t::LoadFunctions(Method MTLCommandBufferRenderCommandEncoderWithDescriptor, Method MTLCommandBufferPresentDrawable)
{
    _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod = MTLCommandBufferRenderCommandEncoderWithDescriptor;
    _MTLCommandBufferPresentDrawableMethod = MTLCommandBufferPresentDrawable;
}

std::weak_ptr<RendererTexture_t> MetalHook_t::AllocImageResource()
{
    return std::shared_ptr<RendererTexture_t>();
}

void MetalHook_t::LoadImageResource(RendererTextureLoadParameter_t& loadParameter)
{
    _ImageResourcesToLoad.emplace_back(loadParameter);
}

void MetalHook_t::ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource)
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
