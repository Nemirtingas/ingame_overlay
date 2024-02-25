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
public:
    static constexpr const char *DLL_NAME = "vulkan-1.dll";

private:
    static VulkanHook_t* _Instance;

    struct VulkanFrame_t
    {
        VkImageView RenderTarget = nullptr;
        VkImage BackBuffer = nullptr;
        VkFramebuffer Framebuffer = nullptr;
        VkCommandPool CommandPool = nullptr;
        VkCommandBuffer CommandBuffer = nullptr;
        VkSemaphore Semaphore = nullptr;
        VkFence Fence = nullptr;
    };

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _Initialized;
    HWND _MainWindow;
    std::function<void* (const char*)> _VulkanFunctionLoader;
    VkPhysicalDevice _VulkanPhysicalDevice;
    VkInstance _VulkanInstance;
    VkDevice _VulkanDevice;
    VkQueue _VulkanQueue;
    uint32_t _QueueFamilyIndex;
    VkDescriptorPool _VulkanDescriptorPool;
    VkRenderPass _VulkanRenderPass;
    std::vector<VulkanFrame_t> _Frames;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    VulkanHook_t();

    void _InitializeForOverlay(VkDevice vulkanDevice, VkSwapchainKHR vulkanSwapChain);
    void _ResetRenderState();
    VulkanFrame_t* _PrepareForOverlay(uint32_t frameIndex);

    static PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName, void* userData);
    PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName);
    bool _FindApplicationHWND();
    void _FreeVulkanRessources();
    bool _CreateVulkanInstance();
    int32_t _GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice);
    bool _GetPhysicalDevice();
    bool _CreateDescriptorPool();
    bool _CreateRenderPass();
    bool _CreateRenderTargets(VkImage* backBuffers, uint32_t backBufferCount);
    bool _SetupVulkanRenderer(VkSwapchainKHR swapChain);

    // Hook to render functions
    decltype(::vkAcquireNextImageKHR)* _VkAcquireNextImageKHR;
    decltype(::vkQueuePresentKHR)* _VkQueuePresentKHR;

    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    decltype(::vkCreateInstance)                         *_vkCreateInstance;
    decltype(::vkDestroyInstance)                        *_vkDestroyInstance;
    decltype(::vkGetInstanceProcAddr)                    *_vkGetInstanceProcAddr;
    decltype(::vkEnumerateInstanceExtensionProperties)   *_vkEnumerateInstanceExtensionProperties;
    decltype(::vkGetDeviceQueue)                         *_vkGetDeviceQueue;
    decltype(::vkQueueSubmit)                            *_vkQueueSubmit;
    decltype(::vkCreateRenderPass)                       *_vkCreateRenderPass;
    decltype(::vkCmdBeginRenderPass)                     *_vkCmdBeginRenderPass;
    decltype(::vkCmdEndRenderPass)                       *_vkCmdEndRenderPass;
    decltype(::vkDestroyRenderPass)                      *_vkDestroyRenderPass;
    decltype(::vkCreateSemaphore)                        *_vkCreateSemaphore;
    decltype(::vkDestroySemaphore)                       *_vkDestroySemaphore;
    decltype(::vkCreateCommandPool)                      *_vkCreateCommandPool;
    decltype(::vkResetCommandPool)                       *_vkResetCommandPool;
    decltype(::vkDestroyCommandPool)                     *_vkDestroyCommandPool;
    decltype(::vkCreateImageView)                        *_vkCreateImageView;
    decltype(::vkDestroyImageView)                       *_vkDestroyImageView;
    decltype(::vkAllocateCommandBuffers)                 *_vkAllocateCommandBuffers;
    decltype(::vkBeginCommandBuffer)                     *_vkBeginCommandBuffer;
    decltype(::vkEndCommandBuffer)                       *_vkEndCommandBuffer;
    decltype(::vkFreeCommandBuffers)                     *_vkFreeCommandBuffers;
    decltype(::vkCreateFramebuffer)                      *_vkCreateFramebuffer;
    decltype(::vkDestroyFramebuffer)                     *_vkDestroyFramebuffer;
    decltype(::vkCreateFence)                            *_vkCreateFence;
    decltype(::vkWaitForFences)                          *_vkWaitForFences;
    decltype(::vkResetFences)                            *_vkResetFences;
    decltype(::vkDestroyFence)                           *_vkDestroyFence;
    decltype(::vkCreateDescriptorPool)                   *_vkCreateDescriptorPool;
    decltype(::vkDestroyDescriptorPool)                  *_vkDestroyDescriptorPool;
    decltype(::vkEnumerateDeviceExtensionProperties)     *_vkEnumerateDeviceExtensionProperties;
    decltype(::vkEnumeratePhysicalDevices)               *_vkEnumeratePhysicalDevices;
    decltype(::vkGetPhysicalDeviceProperties)            *_vkGetPhysicalDeviceProperties;
    decltype(::vkGetPhysicalDeviceQueueFamilyProperties) *_vkGetPhysicalDeviceQueueFamilyProperties;
    decltype(::vkGetSwapchainImagesKHR)                  *_vkGetSwapchainImagesKHR;

public:
    std::string LibraryName;

    virtual ~VulkanHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static VulkanHook_t* Inst();
    virtual const std::string& GetLibraryName() const;
    void LoadFunctions(
        decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR,
        decltype(::vkQueuePresentKHR)* vkQueuePresentKHR,
        std::function<void* (const char*)> vulkanFunctionLoader);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay