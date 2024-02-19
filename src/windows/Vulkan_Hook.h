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

#include <vulkan/vulkan.h>

class Vulkan_Hook :
    public ingame_overlay::Renderer_Hook,
    public Base_Hook
{
public:
    static constexpr const char *DLL_NAME = "vulkan-1.dll";

private:
    static Vulkan_Hook* _inst;

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    HWND _MainWindow;
    VkPhysicalDevice _VulkanPhysicalDevice;
    VkInstance _VulkanInstance;
    VkDevice _VulkanDevice;
    uint32_t _QueueFamilyIndex;
    VkQueue _VulkanQueue;
    VkDescriptorPool _VulkanDescriptorPool;
    // std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    Vulkan_Hook();

    void _ResetRenderState();
    void _PrepareForOverlay(VkCommandBuffer commandBuffer);

    static PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName, void* userData);
    PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName);
    void _FreeVulkanRessources();
    bool _FindApplicationHWND();
    bool _CreateVulkanInstance();
    int32_t _GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice, decltype(::vkGetPhysicalDeviceQueueFamilyProperties)* vkGetPhysicalDeviceQueueFamilyProperties);
    bool _GetPhysicalDeviceAndCreateLogicalDevice();
    bool _CreateDescriptorPool();

    // Hook to render functions
    static VKAPI_ATTR void VKAPI_CALL MyvkCmdEndRenderPass(VkCommandBuffer commandBuffer);
    static VKAPI_ATTR VkResult VKAPI_CALL MyvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    static VKAPI_ATTR VkResult VKAPI_CALL MyvkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);

    decltype(::vkCreateInstance)* _vkCreateInstance;
    decltype(::vkDestroyInstance)* _vkDestroyInstance;
    decltype(::vkGetInstanceProcAddr)* _vkGetInstanceProcAddr;
    decltype(::vkEnumerateInstanceExtensionProperties)* _vkEnumerateInstanceExtensionProperties;
    decltype(::vkAcquireNextImageKHR)* _vkAcquireNextImageKHR;
    decltype(::vkAcquireNextImage2KHR)* _vkAcquireNextImage2KHR;
    decltype(::vkCmdEndRenderPass)* _vkCmdEndRenderPass;

public:
    std::string LibraryName;

    virtual ~Vulkan_Hook();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static Vulkan_Hook* Inst();
    virtual std::string GetLibraryName() const;
    void LoadFunctions(
        decltype(::vkCreateInstance)* vkCreateInstance,
        decltype(::vkDestroyInstance)* vkDestroyInstance,
        decltype(::vkGetInstanceProcAddr)* vkGetInstanceProcAddr,
        decltype(::vkEnumerateInstanceExtensionProperties)* vkEnumerateInstanceExtensionProperties,
        decltype(::vkCmdEndRenderPass)* vkCmdEndRenderPass);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
