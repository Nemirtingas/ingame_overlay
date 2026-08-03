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

static InGameOverlay::ScreenshotDataFormat_t RendererFormatToScreenshotFormat(MTLPixelFormat format)
{
    switch (format)
    {
        // 8-bit
        case MTLPixelFormatRGBA8Unorm:
        case MTLPixelFormatRGBA8Unorm_sRGB:
            return InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;

        case MTLPixelFormatBGRA8Unorm:
        case MTLPixelFormatBGRA8Unorm_sRGB:
            return InGameOverlay::ScreenshotDataFormat_t::B8G8R8A8;

        // 10-bit
        case MTLPixelFormatRGB10A2Unorm:
            return InGameOverlay::ScreenshotDataFormat_t::R10G10B10A2;

        // 16-bit
        case MTLPixelFormatRGBA16Unorm:
            return InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_UNORM;

        case MTLPixelFormatRGBA16Float:
            return InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_FLOAT;

        // 32-bit float
        case MTLPixelFormatRGBA32Float:
            return InGameOverlay::ScreenshotDataFormat_t::R32G32B32A32_FLOAT;

        default:
            return InGameOverlay::ScreenshotDataFormat_t::Unknown;
    }
}

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
void MetalHook_t::_PrepareForOverlay(id<MTLDrawable> drawable, id<MTLTexture> texture, id<MTLCommandBuffer> commandBuffer)
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
    
    MTLRenderPassDescriptor* overlayDescriptor = [[MTLRenderPassDescriptor alloc] init];
    
    overlayDescriptor.colorAttachments[0].texture = texture;
    overlayDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
    overlayDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    
    if (NSViewHook_t::Inst()->PrepareForOverlay() && ImGui_ImplMetal_NewFrame(overlayDescriptor))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot(commandBuffer, drawable);

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

        id<MTLRenderCommandEncoder> renderEncoder = _MTLCommandBufferRenderCommandEncoderWithDescriptor(commandBuffer, @selector(renderCommandEncoderWithDescriptor:), overlayDescriptor);
        
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);

        [renderEncoder endEncoding];
        
        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot(commandBuffer, drawable);
    }
    
    [overlayDescriptor release];
}

void MetalHook_t::_LoadResources()
{
    if (_ImageResourcesToLoad.empty())
        return;

    struct ValidTexture_t
    {
        std::shared_ptr<RendererTexture_t> Resource;
        const void* Data;
        uint32_t Width;
        uint32_t Height;
    };

    std::vector<ValidTexture_t> validResources;

    const auto loadParameterCount =
        std::min(_ImageResourcesToLoad.size(),
                 static_cast<size_t>(_BatchSize));

    for (size_t i = 0; i < loadParameterCount; ++i)
    {
        auto& param = _ImageResourcesToLoad[i];

        auto resource = param.Resource.lock();

        if (!resource)
            continue;

        resource->LoadStatus =
            RendererTextureStatus_e::Loading;

        validResources.push_back({
            std::move(resource),
            param.Data,
            param.Width,
            param.Height
        });
    }

    if (validResources.empty())
    {
        _ImageResourcesToLoad.erase(
            _ImageResourcesToLoad.begin(),
            _ImageResourcesToLoad.begin() + loadParameterCount);

        return;
    }

    for (auto& tex : validResources)
    {
        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:tex.Width
                height:tex.Height
                mipmapped:NO];

        descriptor.usage = MTLTextureUsageShaderRead;

        id<MTLTexture> texture =
            [_MetalDevice newTextureWithDescriptor:descriptor];

        if (texture == nil)
        {
            tex.Resource->LoadStatus =
                RendererTextureStatus_e::NotLoaded;

            continue;
        }

        MTLRegion region =
            MTLRegionMake2D(
                0,
                0,
                tex.Width,
                tex.Height);

        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:tex.Data
                   bytesPerRow:tex.Width * 4];

        tex.Resource->ImGuiTextureId =
            static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(texture));

        tex.Resource->LoadStatus =
            RendererTextureStatus_e::Loaded;
    }

    _ImageResourcesToLoad.erase(
        _ImageResourcesToLoad.begin(),
        _ImageResourcesToLoad.begin() + loadParameterCount);
}

void MetalHook_t::_ReleaseResources()
{
    if (_ImageResourcesToRelease.empty())
        return;

    constexpr uint64_t FramesInFlight = 3;

    auto it = _ImageResourcesToRelease.begin();

    while (it != _ImageResourcesToRelease.end())
    {
        if (_CurrentFrame >=
            it->ReleaseFrame + FramesInFlight)
        {
            it = _ImageResourcesToRelease.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void MetalHook_t::_HandleScreenshot(id<MTLCommandBuffer> commandBuffer, id<MTLDrawable> drawable)
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
            
            inst->_PrepareForOverlay(drawable, renderPass.Descriptor.colorAttachments[0].texture, self);
            
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
    auto ptr = std::shared_ptr<RendererTexture_t>(
        new RendererTexture_t(),
        [](RendererTexture_t* handle)
        {
            if (handle != nullptr)
            {
                if (handle->ImGuiTextureId != 0)
                {
                    id<MTLTexture> texture =
                        (id<MTLTexture>)(uintptr_t)handle->ImGuiTextureId;

                    [texture release];

                    handle->ImGuiTextureId = 0;
                }

                delete handle;
            }
        });

    _ImageResources.emplace(ptr);

    return ptr;
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
