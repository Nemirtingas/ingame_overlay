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

    constexpr static uint32_t MaxDescriptorCountPerPool = 1024;

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

    struct VulkanDescriptorSet_t
    {
        constexpr static uint32_t InvalidDescriptorPoolId = 0xffffffff;

        VkDescriptorSet DescriptorSet = nullptr;
        uint32_t DescriptorPoolId = InvalidDescriptorPoolId;
    };

    struct VulkanDescriptorPool_t
    {
        VkDescriptorPool DescriptorPool;
        uint32_t UsedDescriptors = 0;
    };

    // Variables
    bool _Hooked;
    bool _WindowsHooked;
    bool _RebuildRenderTargets;
    OverlayHookState _HookState;
    HWND _MainWindow;
    std::function<void* (const char*)> _VulkanFunctionLoader;
    VkPhysicalDevice _VulkanPhysicalDevice;
    VkInstance _VulkanInstance;
    VkDevice _VulkanDevice;
    VkQueue _VulkanQueue;
    uint32_t _QueueFamilyIndex;
    VkDescriptorSetLayout _VulkanDescriptorSetLayout;
    std::vector<VulkanDescriptorPool_t> _DescriptorsPools;
    VkSampler _VulkanImageSampler;
    VkCommandPool _VulkanImageCommandPool;
    VkCommandBuffer _VulkanImageCommandBuffer;
    VkRenderPass _VulkanRenderPass;
    std::vector<VulkanFrame_t> _Frames;
    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    VulkanHook_t();

    bool _AllocDescriptorPool();
    VulkanDescriptorSet_t _GetFreeDescriptorSetFromPool(uint32_t poolIndex);
    VulkanDescriptorSet_t _GetFreeDescriptorSet();
    void _ReleaseDescriptor(uint32_t id, VkDescriptorSet descriptorSet);
    void _CreateImageTexture(VkDescriptorSet descriptorSet, VkImageView imageView, VkImageLayout imageLayout);

    bool _CreateRenderTargets(VkSwapchainKHR swapChain);
    void _DestroyRenderTargets();
    void _ResetRenderState(OverlayHookState state);
    void _InitializeForOverlay(VkDevice vulkanDevice, VkSwapchainKHR vulkanSwapChain);
    VulkanFrame_t* _PrepareForOverlay(uint32_t frameIndex);

    static PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName, void* userData);
    PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName);
    bool _FindApplicationHWND();
    void _FreeVulkanRessources();
    bool _CreateVulkanInstance();
    int32_t _GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice);
    bool _GetPhysicalDevice();
    bool _CreateRenderPass();
    void _DestroyRenderPass();
    bool _SetupVulkanRenderer();
    uint32_t _GetVulkanMemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits);
    bool _CreateImageObjects();

    // Hook to render functions
    decltype(::vkAcquireNextImageKHR)* _VkAcquireNextImageKHR;
    decltype(::vkQueuePresentKHR)* _VkQueuePresentKHR;
    decltype(::vkDestroyDevice)* _VkDestroyDevice;

    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
    static VKAPI_ATTR void     VKAPI_CALL _MyVkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);

    decltype(::vkDeviceWaitIdle)* _vkDeviceWaitIdle;

    decltype(::vkCreateInstance)                         *_vkCreateInstance;
    decltype(::vkDestroyInstance)                        *_vkDestroyInstance;
    decltype(::vkGetInstanceProcAddr)                    *_vkGetInstanceProcAddr;
    decltype(::vkEnumerateInstanceExtensionProperties)   *_vkEnumerateInstanceExtensionProperties;
    decltype(::vkGetDeviceQueue)                         *_vkGetDeviceQueue;
    decltype(::vkQueueSubmit)                            *_vkQueueSubmit;
    decltype(::vkQueueWaitIdle)                          *_vkQueueWaitIdle;
    decltype(::vkCreateRenderPass)                       *_vkCreateRenderPass;
    decltype(::vkCmdBeginRenderPass)                     *_vkCmdBeginRenderPass;
    decltype(::vkCmdEndRenderPass)                       *_vkCmdEndRenderPass;
    decltype(::vkDestroyRenderPass)                      *_vkDestroyRenderPass;
    decltype(::vkCreateSemaphore)                        *_vkCreateSemaphore;
    decltype(::vkDestroySemaphore)                       *_vkDestroySemaphore;
    decltype(::vkCreateBuffer)                           *_vkCreateBuffer;
    decltype(::vkDestroyBuffer)                          *_vkDestroyBuffer;
    decltype(::vkMapMemory)                              *_vkMapMemory;
    decltype(::vkUnmapMemory)                            *_vkUnmapMemory;
    decltype(::vkFlushMappedMemoryRanges)                *_vkFlushMappedMemoryRanges;
    decltype(::vkBindBufferMemory)                       *_vkBindBufferMemory;
    decltype(::vkCmdCopyBufferToImage)                   *_vkCmdCopyBufferToImage;
    decltype(::vkBindImageMemory)                        *_vkBindImageMemory;
    decltype(::vkCreateCommandPool)                      *_vkCreateCommandPool;
    decltype(::vkResetCommandPool)                       *_vkResetCommandPool;
    decltype(::vkDestroyCommandPool)                     *_vkDestroyCommandPool;
    decltype(::vkCreateImageView)                        *_vkCreateImageView;
    decltype(::vkDestroyImageView)                       *_vkDestroyImageView;
    decltype(::vkCreateSampler)                          *_vkCreateSampler;
    decltype(::vkDestroySampler)                         *_vkDestroySampler;
    decltype(::vkCreateImage)                            *_vkCreateImage;
    decltype(::vkDestroyImage)                           *_vkDestroyImage;
    decltype(::vkAllocateMemory)                         *_vkAllocateMemory;
    decltype(::vkFreeMemory)                             *_vkFreeMemory;
    decltype(::vkCmdPipelineBarrier)                     *_vkCmdPipelineBarrier;
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
    decltype(::vkCreateDescriptorSetLayout)              *_vkCreateDescriptorSetLayout;
    decltype(::vkDestroyDescriptorSetLayout)             *_vkDestroyDescriptorSetLayout;
    decltype(::vkAllocateDescriptorSets)                 *_vkAllocateDescriptorSets;
    decltype(::vkUpdateDescriptorSets)                   *_vkUpdateDescriptorSets;
    decltype(::vkFreeDescriptorSets)                     *_vkFreeDescriptorSets;
    decltype(::vkGetBufferMemoryRequirements)            *_vkGetBufferMemoryRequirements;
    decltype(::vkGetImageMemoryRequirements)             *_vkGetImageMemoryRequirements;
    decltype(::vkEnumerateDeviceExtensionProperties)     *_vkEnumerateDeviceExtensionProperties;
    decltype(::vkEnumeratePhysicalDevices)               *_vkEnumeratePhysicalDevices;
    decltype(::vkGetPhysicalDeviceProperties)            *_vkGetPhysicalDeviceProperties;
    decltype(::vkGetPhysicalDeviceQueueFamilyProperties) *_vkGetPhysicalDeviceQueueFamilyProperties;
    decltype(::vkGetPhysicalDeviceMemoryProperties)      *_vkGetPhysicalDeviceMemoryProperties;
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
        decltype(::vkDestroyDevice)* vkDestroyDevice,
        std::function<void* (const char*)> vulkanFunctionLoader);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay