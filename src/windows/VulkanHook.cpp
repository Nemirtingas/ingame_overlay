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

#include "VulkanHook.h"
#include "WindowsHook.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

namespace InGameOverlay {

VulkanHook_t* VulkanHook_t::_Instance = nullptr;

bool VulkanHook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    SPDLOG_WARN("Vulkan overlay is not yet supported.");
    return false;
    if (!_Hooked)
    {
        if (_VkQueuePresentKHR == nullptr)
        {
            SPDLOG_WARN("Failed to hook Vulkan: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        SPDLOG_INFO("Hooked Vulkan");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)_VkQueuePresentKHR, &VulkanHook_t::_MyVkQueuePresentKHR)
        );
        EndHook();
    }
    return true;
}

void VulkanHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideAppInputs(hide);
    }
}

void VulkanHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideOverlayInputs(hide);
    }
}

bool VulkanHook_t::IsStarted()
{
    return _Hooked;
}

void VulkanHook_t::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(InGameOverlay::OverlayHookState::Removing);

        ImGui_ImplVulkan_Shutdown();
        Windows_Hook::Inst()->ResetRenderState();
        ImGui::DestroyContext();

        //_ImageResources.clear();

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void VulkanHook_t::_PrepareForOverlay()
{
    
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto inst = VulkanHook_t::Inst();
    inst->_PrepareForOverlay();
    return inst->_VkQueuePresentKHR(queue, pPresentInfo);
}

VulkanHook_t::VulkanHook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    _ImGuiFontAtlas(nullptr),
    _VkQueuePresentKHR(nullptr)
{
}

VulkanHook_t::~VulkanHook_t()
{
    SPDLOG_INFO("VulkanHook_t Hook removed");

    if (_WindowsHooked)
        delete Windows_Hook::Inst();

    if (_Initialized)
    {
    }

    _Instance = nullptr;
}

VulkanHook_t* VulkanHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new VulkanHook_t;

    return _Instance;
}

std::string VulkanHook_t::GetLibraryName() const
{
    return LibraryName;
}

void VulkanHook_t::LoadFunctions(decltype(::vkQueuePresentKHR)* _vkQueuePresentKHR)
{
    _VkQueuePresentKHR = _vkQueuePresentKHR;
}

std::weak_ptr<uint64_t> VulkanHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>(nullptr);
}

void VulkanHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{

}

}// namespace InGameOverlay