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
#include <backends/imgui_impl_metal.h>

namespace InGameOverlay {

MetalHook_t* MetalHook_t::_Instance = nullptr;

bool MetalHook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod == nil || _MTLRenderCommandEncoderEndEncodingMethod == nil)
        {
            INGAMEOVERLAY_WARN("Failed to hook Metal: Rendering functions missing.");
            return false;
        }

        if (!NSViewHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;
        
        _NSViewHooked = true;

        _MTLCommandBufferRenderCommandEncoderWithDescriptor = (decltype(_MTLCommandBufferRenderCommandEncoderWithDescriptor))method_setImplementation(_MTLCommandBufferRenderCommandEncoderWithDescriptorMethod, (IMP)&MyMTLCommandBufferRenderCommandEncoderWithDescriptor);
        _MTLRenderCommandEncoderEndEncoding = (decltype(_MTLRenderCommandEncoderEndEncoding))method_setImplementation(_MTLRenderCommandEncoderEndEncodingMethod, (IMP)&MyMTLCommandEncoderEndEncoding);

        INGAMEOVERLAY_INFO("Hooked Metal");
        _Hooked = true;
        _ImGuiFontAtlas = imgui_font_atlas;
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
void MetalHook_t::_PrepareForOverlay(RenderPass_t& renderPass)
{
    if (!_Initialized)
    {
        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));
        
        _MetalDevice = [renderPass.CommandBuffer device];

        ImGui_ImplMetal_Init(_MetalDevice);
        
        _Initialized = true;
        OverlayHookReady(InGameOverlay::OverlayHookState::Ready);
    }
    
    if (NSViewHook_t::Inst()->PrepareForOverlay() && ImGui_ImplMetal_NewFrame(renderPass.Descriptor))
    {
        ImGui::NewFrame();
        
        OverlayProc();
        
        ImGui::Render();

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), renderPass.CommandBuffer, renderPass.Encoder);
    }
}

id<MTLRenderCommandEncoder> MetalHook_t::MyMTLCommandBufferRenderCommandEncoderWithDescriptor(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor)
{
    MetalHook_t* inst = MetalHook_t::Inst();
    id<MTLRenderCommandEncoder> encoder = inst->_MTLCommandBufferRenderCommandEncoderWithDescriptor(self, sel, descriptor);
    
    inst->_RenderPass.emplace_back(RenderPass_t{
        descriptor,
        self,
        encoder,
    });
    
    return encoder;
}

void MetalHook_t::MyMTLCommandEncoderEndEncoding(id<MTLRenderCommandEncoder> self, SEL sel)
{
    MetalHook_t* inst = MetalHook_t::Inst();

    for(auto it = inst->_RenderPass.begin(); it != inst->_RenderPass.end(); ++it)
    {
        if(it->Encoder == self)
        {
            inst->_PrepareForOverlay(*it);
            inst->_RenderPass.erase(it);
            break;
        }
    }
    
    inst->_MTLRenderCommandEncoderEndEncoding(self, sel);
}

MetalHook_t::MetalHook_t():
    _Initialized(false),
    _Hooked(false),
    _ImGuiFontAtlas(nullptr),
    _MetalDevice(nil),
    _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod(nil),
    _MTLRenderCommandEncoderEndEncodingMethod(nil),
    _MTLCommandBufferRenderCommandEncoderWithDescriptor(nullptr),
    _MTLRenderCommandEncoderEndEncoding(nullptr)
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
    if (_MTLRenderCommandEncoderEndEncodingMethod != nil && _MTLRenderCommandEncoderEndEncoding != nullptr)
    {
        method_setImplementation(_MTLRenderCommandEncoderEndEncodingMethod, (IMP)_MTLRenderCommandEncoderEndEncoding);
        _MTLRenderCommandEncoderEndEncoding = nullptr;
    }

    if (_Initialized)
    {
        ImGui_ImplMetal_Shutdown();
        ImGui::DestroyContext();
        _MetalDevice = nil;
    }

    _Instance = nullptr;
}

MetalHook_t* MetalHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new MetalHook_t;

    return _Instance;
}

const std::string& MetalHook_t::GetLibraryName() const
{
    return LibraryName;
}

RendererHookType_t MetalHook_t::GetRendererHookType() const
{
    return RendererHookType_t::Metal;
}

void MetalHook_t::LoadFunctions(Method MTLCommandBufferRenderCommandEncoderWithDescriptor, Method RenderCommandEncoderEndEncoding)
{
    _MTLCommandBufferRenderCommandEncoderWithDescriptorMethod = MTLCommandBufferRenderCommandEncoderWithDescriptor;
    _MTLRenderCommandEncoderEndEncodingMethod = RenderCommandEncoderEndEncoding;
}

std::weak_ptr<uint64_t> MetalHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>();
}

void MetalHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
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