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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <vulkan/vulkan.h>

namespace InGameOverlay {

class VulkanHook_t :
    public RendererHook_t,
    public BaseHook_t
{
private:
    static VulkanHook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    // std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    VulkanHook_t();

    void _ResetRenderState(OverlayHookState state);
    void _PrepareForOverlay();

    // Hook to render functions
    decltype(::vkQueuePresentKHR)* _VkQueuePresentKHR;

    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

public:
    std::string LibraryName;

    virtual ~VulkanHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static VulkanHook_t* Inst();
    virtual const std::string& GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(decltype(::vkQueuePresentKHR)* _vkQueuePresentKHR);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay