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
    constexpr static uint32_t MaxDescriptorCountPerPool = 1024;

private:
    static VulkanHook_t* _Instance;

    struct VulkanFrame_t
    {
        VkImageView RenderTarget = VK_NULL_HANDLE;
        VkImage BackBuffer = VK_NULL_HANDLE;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore RenderCompleteSemaphore = VK_NULL_HANDLE;
        VkSemaphore ImageAcquiredSemaphore = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
    };

    struct VulkanDescriptorSet_t
    {
        constexpr static uint32_t InvalidDescriptorPoolId = 0xffffffff;

        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
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
    OverlayHookState _HookState;
    HWND _MainWindow;

    std::function<void* (const char*)> _VulkanLoader;
    VkAllocationCallbacks* _VulkanAllocationCallbacks;
    VkInstance _VulkanInstance;
    VkPhysicalDevice _VulkanPhysicalDevice;
    std::vector<VkQueueFamilyProperties> _VulkanQueueFamilies;
    uint32_t _VulkanQueueFamily;
    VkCommandPool _VulkanImageCommandPool;
    VkCommandBuffer _VulkanImageCommandBuffer;
    VkSampler _VulkanImageSampler;
    VkDescriptorSetLayout _VulkanImageDescriptorSetLayout;
    std::vector<VulkanFrame_t> _Frames;
    VkRenderPass _VulkanRenderPass;
    std::vector<VulkanDescriptorPool_t> _DescriptorsPools;

    VkDevice _VulkanDevice;
    VkQueue _VulkanQueue;

    VulkanDescriptorSet_t _ImGuiFontDescriptor;

    std::set<std::shared_ptr<uint64_t>> _ImageResources;
    void* _ImGuiFontAtlas;

    // Functions
    VulkanHook_t();

    bool _AllocDescriptorPool();
    VulkanDescriptorSet_t _GetFreeDescriptorSetFromPool(uint32_t poolIndex);
    VulkanDescriptorSet_t _GetFreeDescriptorSet();
    void _ReleaseDescriptor(VulkanDescriptorSet_t descriptorSet);
    void _DestroyDescriptorPools();
    void _CreateImageTexture(VkDescriptorSet descriptorSet, VkImageView imageView, VkImageLayout imageLayout);

    bool _CreateRenderTargets(VkSwapchainKHR swapChain);
    void _DestroyRenderTargets();
    void _ResetRenderState(OverlayHookState state);

    void _PrepareForOverlay(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    static PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName, void* userData);
    PFN_vkVoidFunction _LoadVulkanFunction(const char* functionName);
    void _FreeVulkanRessources();
    bool _CreateVulkanInstance();
    int32_t _GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice);
    bool _GetPhysicalDevice();

    bool _CreateImageSampler();
    void _DestroyImageSampler();

    bool _CreateImageDescriptorSetLayout();
    void _DestroyImageDescriptorSetLayout();

    bool _CreateImageCommandPool();
    void _DestroyImageCommandPool();

    bool _CreateImageCommandBuffer();
    void _DestroyImageCommandBuffer();

    bool _CreateImageDevices();
    void _DestroyImageDevices();

    bool _CreateRenderPass();
    void _DestroyRenderPass();

    uint32_t _GetVulkanMemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits);
    bool _DoesQueueSupportGraphic(VkQueue queue);

    // Hook to render functions
    decltype(::vkAcquireNextImageKHR) * _VkAcquireNextImageKHR;
    decltype(::vkAcquireNextImage2KHR)* _VkAcquireNextImage2KHR;
    decltype(::vkQueuePresentKHR)     * _VkQueuePresentKHR;
    decltype(::vkCreateSwapchainKHR)  * _VkCreateSwapchainKHR;
    decltype(::vkDestroyDevice)       * _VkDestroyDevice;

    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);
    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
    static VKAPI_ATTR VkResult VKAPI_CALL _MyVkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
    static VKAPI_ATTR void     VKAPI_CALL _MyVkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);

    decltype(::vkCreateInstance)                         *_vkCreateInstance;
    decltype(::vkDestroyInstance)                        *_vkDestroyInstance;
    decltype(::vkGetInstanceProcAddr)                    *_vkGetInstanceProcAddr;
    decltype(::vkDeviceWaitIdle)                         *_vkDeviceWaitIdle;
    decltype(::vkGetDeviceProcAddr)                      *_vkGetDeviceProcAddr;
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
    decltype(::vkResetCommandBuffer)                     *_vkResetCommandBuffer;
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
    decltype(::vkEnumeratePhysicalDevices)               *_vkEnumeratePhysicalDevices;
    decltype(::vkGetPhysicalDeviceProperties)            *_vkGetPhysicalDeviceProperties;
    decltype(::vkGetPhysicalDeviceQueueFamilyProperties) *_vkGetPhysicalDeviceQueueFamilyProperties;
    decltype(::vkGetPhysicalDeviceMemoryProperties)      *_vkGetPhysicalDeviceMemoryProperties;
    decltype(::vkEnumerateDeviceExtensionProperties)     *_vkEnumerateDeviceExtensionProperties;

public:
    std::string LibraryName;

    virtual ~VulkanHook_t();

    virtual bool StartHook(std::function<void()> key_combination_callback, std::set<ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas = nullptr);
    virtual void HideAppInputs(bool hide);
    virtual void HideOverlayInputs(bool hide);
    virtual bool IsStarted();
    static VulkanHook_t* Inst();
    virtual const std::string& GetLibraryName() const;
    virtual RendererHookType_t GetRendererHookType() const;
    void LoadFunctions(
        std::function<void*(const char*)> vkLoader,
        decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR,
        decltype(::vkAcquireNextImage2KHR)* vkAcquireNextImage2KHR,
        decltype(::vkQueuePresentKHR)* vkQueuePresentKHR,
        decltype(::vkCreateSwapchainKHR)* vkCreateSwapchainKHR,
        decltype(::vkDestroyDevice)* vkDestroyDevice);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};

}// namespace InGameOverlay