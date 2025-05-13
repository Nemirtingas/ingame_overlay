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
#include "X11Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include "../VulkanHelpers.h"

#include <unistd.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&VulkanHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    return false;\
} } while(0)

VulkanHook_t* VulkanHook_t::_Instance = nullptr;

static inline VkResult _CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return err;

    INGAMEOVERLAY_ERROR("[vulkan] Error: VkResult = {}", (int)err);
    return err;
}

static inline uint32_t MakeImageDescriptorId(uint32_t descriptorIndex, uint32_t usedIndex)
{
    return VulkanHook_t::MaxDescriptorCountPerPool * descriptorIndex + usedIndex;
}

static inline uint32_t GetImageDescriptorPool(uint32_t descriptorId)
{
    return descriptorId / VulkanHook_t::MaxDescriptorCountPerPool;
}

static InGameOverlay::ScreenshotBufferFormat_t RendererFormatToScreenshotFormat(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_R8G8B8A8_UNORM          : return ScreenshotBufferFormat_t::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB           : return ScreenshotBufferFormat_t::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM          : return ScreenshotBufferFormat_t::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB           : return ScreenshotBufferFormat_t::B8G8R8A8_SRGB;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return ScreenshotBufferFormat_t::R10G10B10A2;
        case VK_FORMAT_R16G16B16A16_SFLOAT     : return ScreenshotBufferFormat_t::R16G16B16A16_FLOAT;
        case VK_FORMAT_R16G16B16A16_UNORM      : return ScreenshotBufferFormat_t::R16G16B16A16_UNORM;
        case VK_FORMAT_R32G32B32A32_SFLOAT     : return ScreenshotBufferFormat_t::R32G32B32A32_FLOAT;
        case VK_FORMAT_B5G6R5_UNORM_PACK16     : return ScreenshotBufferFormat_t::B5G6R5;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16   : return ScreenshotBufferFormat_t::B5G5R5A1;
        default:                                 return ScreenshotBufferFormat_t::Unknown;
    }
}

bool VulkanHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_VkAcquireNextImageKHR == nullptr || _VkQueuePresentKHR == nullptr || _VkCreateSwapchainKHR == nullptr || _VkDestroyDevice  == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook Vulkan: Rendering functions missing.");
            return false;
        }

        if (!_CreateVulkanInstance())
            return false;

        if (!X11Hook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _X11Hooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION_OR_FAIL(VkAcquireNextImageKHR);
        if (_VkAcquireNextImage2KHR != nullptr)
            TRY_HOOK_FUNCTION_OR_FAIL(VkAcquireNextImage2KHR);

        TRY_HOOK_FUNCTION_OR_FAIL(VkQueuePresentKHR);
        TRY_HOOK_FUNCTION_OR_FAIL(VkCreateSwapchainKHR);
        TRY_HOOK_FUNCTION_OR_FAIL(VkDestroyDevice);
        EndHook();

        INGAMEOVERLAY_INFO("Hooked Vulkan");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void VulkanHook_t::HideAppInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        X11Hook_t::Inst()->HideAppInputs(hide);
}

void VulkanHook_t::HideOverlayInputs(bool hide)
{
    if (_HookState == OverlayHookState::Ready)
        X11Hook_t::Inst()->HideOverlayInputs(hide);
}

bool VulkanHook_t::IsStarted()
{
    return _Hooked;
}

bool VulkanHook_t::_AllocDescriptorPool()
{    
    VkDescriptorPool vulkanDescriptorPool = VK_NULL_HANDLE;

    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDescriptorCountPerPool },
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCreateInfo.maxSets = MaxDescriptorCountPerPool;
    descriptorPoolCreateInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    if (_vkCreateDescriptorPool(_VulkanDevice, &descriptorPoolCreateInfo, _VulkanAllocationCallbacks, &vulkanDescriptorPool) != VkResult::VK_SUCCESS || vulkanDescriptorPool == VK_NULL_HANDLE)
        return false;
    
    _DescriptorsPools.emplace_back(VulkanDescriptorPool_t
    {
        vulkanDescriptorPool,
        0
    });

    return true;
}

VulkanHook_t::VulkanDescriptorSet_t VulkanHook_t::_GetFreeDescriptorSetFromPool(uint32_t poolIndex)
{
    auto& descriptorsPool = _DescriptorsPools[poolIndex];
    VkDescriptorSet vulkanDescriptorSet = VK_NULL_HANDLE;
    VulkanDescriptorSet_t descriptorSet;
    
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptorsPool.DescriptorPool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &_VulkanImageDescriptorSetLayout;
    auto result = _vkAllocateDescriptorSets(_VulkanDevice, &alloc_info, &vulkanDescriptorSet);
    
    descriptorSet.DescriptorPoolId = MakeImageDescriptorId(poolIndex, descriptorsPool.UsedDescriptors++);
    descriptorSet.DescriptorSet = vulkanDescriptorSet;
    
    return descriptorSet;
}

VulkanHook_t::VulkanDescriptorSet_t VulkanHook_t::_GetFreeDescriptorSet()
{
    for (uint32_t poolIndex = 0; poolIndex < _DescriptorsPools.size(); ++poolIndex)
    {
        if (_DescriptorsPools[poolIndex].UsedDescriptors < MaxDescriptorCountPerPool)
            return _GetFreeDescriptorSetFromPool(poolIndex);
    }

    if (!_AllocDescriptorPool())
        return {};

    return _GetFreeDescriptorSetFromPool(_DescriptorsPools.size() - 1);
}

void VulkanHook_t::_ReleaseDescriptor(VulkanDescriptorSet_t descriptorSet)
{
    auto& pool = _DescriptorsPools[GetImageDescriptorPool(descriptorSet.DescriptorPoolId)];

    _vkFreeDescriptorSets(_VulkanDevice, pool.DescriptorPool, 1, &descriptorSet.DescriptorSet);
    --pool.UsedDescriptors;
}

void VulkanHook_t::_DestroyDescriptorPools()
{
    for (auto& pool : _DescriptorsPools)
        _vkDestroyDescriptorPool(_VulkanDevice, pool.DescriptorPool, _VulkanAllocationCallbacks);

    _DescriptorsPools.clear();
}

void VulkanHook_t::_CreateImageTexture(VkDescriptorSet descriptorSet, VkImageView imageView, VkImageLayout imageLayout)
{
    VkDescriptorImageInfo descriptorImageInfo[1] = {};
    descriptorImageInfo[0].sampler = _VulkanImageSampler;
    descriptorImageInfo[0].imageView = imageView;
    descriptorImageInfo[0].imageLayout = imageLayout;
    VkWriteDescriptorSet writeDescriptorSet[1] = {};
    writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[0].dstSet = descriptorSet;
    writeDescriptorSet[0].descriptorCount = 1;
    writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet[0].pImageInfo = descriptorImageInfo;
    _vkUpdateDescriptorSets(_VulkanDevice, 1, writeDescriptorSet, 0, nullptr);
}

bool VulkanHook_t::_CreateRenderTargets(VkSwapchainKHR swapChain)
{
    auto vkGetSwapchainImagesKHR = (decltype(::vkGetSwapchainImagesKHR)*)_vkGetDeviceProcAddr(_VulkanDevice, "vkGetSwapchainImagesKHR");

    uint32_t swapchainImageCount;
    vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &swapchainImageCount, NULL);

    std::vector<VkImage> backbuffers(swapchainImageCount);
    vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &swapchainImageCount, backbuffers.data());

    _Frames.resize(swapchainImageCount);

    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        auto& frame = _Frames[i];

        frame.BackBuffer = backbuffers[i];

        {
            VkCommandPoolCreateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = _VulkanQueueFamily;

            if (_vkCreateCommandPool(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.CommandPool) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
        {
            VkCommandBufferAllocateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = frame.CommandPool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;

            if (_vkAllocateCommandBuffers(_VulkanDevice, &info, &frame.CommandBuffer) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
        {
            VkFenceCreateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (_vkCreateFence(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.Fence) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
        {
            VkSemaphoreCreateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            if (_vkCreateSemaphore(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.ImageAcquiredSemaphore) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
            if (_vkCreateSemaphore(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.RenderCompleteSemaphore) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
        {
            VkImageViewCreateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = _VulkanTargetFormat;
            info.image = frame.BackBuffer;

            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;

            if (_vkCreateImageView(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.RenderTarget) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
        {
            VkImageView attachment[1] = { frame.RenderTarget };

            VkFramebufferCreateInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = _VulkanRenderPass;
            info.attachmentCount = 1;
            info.pAttachments = attachment;
            info.layers = 1;
            info.width = ImGui::GetIO().DisplaySize.x;
            info.height = ImGui::GetIO().DisplaySize.y;

            if (_vkCreateFramebuffer(_VulkanDevice, &info, _VulkanAllocationCallbacks, &frame.Framebuffer) != VkResult::VK_SUCCESS)
            {
                _DestroyRenderTargets();
                return false;
            }
        }
    }

    return true;
}

void VulkanHook_t::_DestroyRenderTargets()
{
    for (auto& frame : _Frames)
    {
        if (frame.Fence)
            _vkDestroyFence(_VulkanDevice, frame.Fence, _VulkanAllocationCallbacks);

        if (frame.CommandBuffer)
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);

        if (frame.CommandPool)
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, _VulkanAllocationCallbacks);

        if (frame.RenderTarget)
            _vkDestroyImageView(_VulkanDevice, frame.RenderTarget, _VulkanAllocationCallbacks);

        if (frame.Framebuffer)
            _vkDestroyFramebuffer(_VulkanDevice, frame.Framebuffer, _VulkanAllocationCallbacks);

        if (frame.ImageAcquiredSemaphore)
            _vkDestroySemaphore(_VulkanDevice, frame.ImageAcquiredSemaphore, _VulkanAllocationCallbacks);

        if (frame.RenderCompleteSemaphore)
            _vkDestroySemaphore(_VulkanDevice, frame.RenderCompleteSemaphore, _VulkanAllocationCallbacks);
    }
    _Frames.clear();
}

void VulkanHook_t::_ResetRenderState(OverlayHookState state)
{
    if (_HookState == state)
        return;

    OverlayHookReady(state);

    _HookState = state;
    switch (state)
    {
        case OverlayHookState::Removing:
            ImGui_ImplVulkan_Shutdown();
            X11Hook_t::Inst()->ResetRenderState(state);
            ImGui::DestroyContext();

            _ImageResources.clear();

            _FreeVulkanRessources();

            _SentOutOfDate = false;
    }
}

PFN_vkVoidFunction VulkanHook_t::_LoadVulkanFunction(const char* functionName, void* userData)
{
    return reinterpret_cast<VulkanHook_t*>(userData)->_LoadVulkanFunction(functionName);
}

PFN_vkVoidFunction VulkanHook_t::_LoadVulkanFunction(const char* functionName)
{
    return _vkGetInstanceProcAddr(_VulkanInstance, functionName);
}

void VulkanHook_t::_FreeVulkanRessources()
{
    _DestroyRenderTargets();
    _DestroyImageDevices();

    _ReleaseDescriptor(_ImGuiFontDescriptor);
    _DestroyDescriptorPools();

    _VulkanQueue = nullptr;
    _VulkanDevice = nullptr;
}

bool VulkanHook_t::_CreateVulkanInstance()
{
    _vkGetInstanceProcAddr = (decltype(::vkGetInstanceProcAddr)*)_VulkanLoader("vkGetInstanceProcAddr");
    _vkCreateInstance = (decltype(::vkCreateInstance)*)_VulkanLoader("vkCreateInstance");
    _vkDestroyInstance = (decltype(::vkDestroyInstance)*)_VulkanLoader("_vkDestroyInstance");

    // Create Vulkan Instance
    {
        _VulkanInstance = nullptr;
        VkInstanceCreateInfo createInfo = { };
        constexpr const char* instance_extension = VK_KHR_SURFACE_EXTENSION_NAME;

        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &instance_extension;

        // Create Vulkan Instance without any debug feature
        _vkCreateInstance(&createInfo, _VulkanAllocationCallbacks, &_VulkanInstance);
    }

#define LOAD_VULKAN_FUNCTION(NAME) if (_##NAME == nullptr) _##NAME = (decltype(_##NAME))_LoadVulkanFunction(#NAME)
    LOAD_VULKAN_FUNCTION(vkGetDeviceProcAddr);
    LOAD_VULKAN_FUNCTION(vkDeviceWaitIdle);

    LOAD_VULKAN_FUNCTION(vkQueueSubmit);
    LOAD_VULKAN_FUNCTION(vkQueueWaitIdle);
    LOAD_VULKAN_FUNCTION(vkGetDeviceQueue);
    LOAD_VULKAN_FUNCTION(vkCreateRenderPass);
    LOAD_VULKAN_FUNCTION(vkCmdBeginRenderPass);
    LOAD_VULKAN_FUNCTION(vkCmdEndRenderPass);
    LOAD_VULKAN_FUNCTION(vkDestroyRenderPass);
    LOAD_VULKAN_FUNCTION(vkCmdCopyImage);
    LOAD_VULKAN_FUNCTION(vkGetImageSubresourceLayout);
    LOAD_VULKAN_FUNCTION(vkCreateSemaphore);
    LOAD_VULKAN_FUNCTION(vkDestroySemaphore);
    LOAD_VULKAN_FUNCTION(vkCreateBuffer);
    LOAD_VULKAN_FUNCTION(vkDestroyBuffer);
    LOAD_VULKAN_FUNCTION(vkMapMemory);
    LOAD_VULKAN_FUNCTION(vkUnmapMemory);
    LOAD_VULKAN_FUNCTION(vkFlushMappedMemoryRanges);
    LOAD_VULKAN_FUNCTION(vkBindBufferMemory);
    LOAD_VULKAN_FUNCTION(vkCmdCopyBufferToImage);
    LOAD_VULKAN_FUNCTION(vkBindImageMemory);
    LOAD_VULKAN_FUNCTION(vkCreateCommandPool);
    LOAD_VULKAN_FUNCTION(vkResetCommandPool);
    LOAD_VULKAN_FUNCTION(vkDestroyCommandPool);
    LOAD_VULKAN_FUNCTION(vkCreateImageView);
    LOAD_VULKAN_FUNCTION(vkDestroyImageView);
    LOAD_VULKAN_FUNCTION(vkCreateSampler);
    LOAD_VULKAN_FUNCTION(vkDestroySampler);
    LOAD_VULKAN_FUNCTION(vkCreateImage);
    LOAD_VULKAN_FUNCTION(vkDestroyImage);
    LOAD_VULKAN_FUNCTION(vkAllocateMemory);
    LOAD_VULKAN_FUNCTION(vkFreeMemory);
    LOAD_VULKAN_FUNCTION(vkCmdPipelineBarrier);
    LOAD_VULKAN_FUNCTION(vkAllocateCommandBuffers);
    LOAD_VULKAN_FUNCTION(vkBeginCommandBuffer);
    LOAD_VULKAN_FUNCTION(vkResetCommandBuffer);
    LOAD_VULKAN_FUNCTION(vkEndCommandBuffer);
    LOAD_VULKAN_FUNCTION(vkFreeCommandBuffers);
    LOAD_VULKAN_FUNCTION(vkCreateFramebuffer);
    LOAD_VULKAN_FUNCTION(vkDestroyFramebuffer);
    LOAD_VULKAN_FUNCTION(vkCreateFence);
    LOAD_VULKAN_FUNCTION(vkWaitForFences);
    LOAD_VULKAN_FUNCTION(vkResetFences);
    LOAD_VULKAN_FUNCTION(vkDestroyFence);
    LOAD_VULKAN_FUNCTION(vkCreateDescriptorPool);
    LOAD_VULKAN_FUNCTION(vkDestroyDescriptorPool);
    LOAD_VULKAN_FUNCTION(vkCreateDescriptorSetLayout);
    LOAD_VULKAN_FUNCTION(vkDestroyDescriptorSetLayout);
    LOAD_VULKAN_FUNCTION(vkAllocateDescriptorSets);
    LOAD_VULKAN_FUNCTION(vkUpdateDescriptorSets);
    LOAD_VULKAN_FUNCTION(vkFreeDescriptorSets);
    LOAD_VULKAN_FUNCTION(vkGetBufferMemoryRequirements);
    LOAD_VULKAN_FUNCTION(vkGetImageMemoryRequirements);
    LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceMemoryProperties);
    LOAD_VULKAN_FUNCTION(vkEnumerateDeviceExtensionProperties);
    LOAD_VULKAN_FUNCTION(vkEnumeratePhysicalDevices);
    LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceProperties);
    LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
#undef LOAD_VULKAN_FUNCTION

    if (!_GetPhysicalDevice())
    {
        _vkDestroyInstance(_VulkanInstance, _VulkanAllocationCallbacks);
        return false;
    }

    // Select graphics queue family
    {
        _VulkanQueueFamily = uint32_t(-1);
        uint32_t count;
        _vkGetPhysicalDeviceQueueFamilyProperties(_VulkanPhysicalDevice, &count, NULL);
        _VulkanQueueFamilies.resize(count);
        _vkGetPhysicalDeviceQueueFamilyProperties(_VulkanPhysicalDevice, &count, _VulkanQueueFamilies.data());
        for (uint32_t i = 0; i < count; ++i)
        {
            if (_VulkanQueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                _VulkanQueueFamily = i;
                break;
            }
        }

        if (_VulkanQueueFamily == uint32_t(-1))
        {
            _vkDestroyInstance(_VulkanInstance, _VulkanAllocationCallbacks);
            return false;
        }
    }

    return true;
}

int32_t VulkanHook_t::_GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice)
{
    uint32_t count;
    std::vector<VkQueueFamilyProperties> queues;
    _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    queues.resize(count);
    _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queues.data());
    for (uint32_t i = 0; i < count; i++)
    {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return i;
    }

    return -1;
}

bool VulkanHook_t::_GetPhysicalDevice()
{
    _VulkanPhysicalDevice = nullptr;
    uint32_t physicalDeviceCount;
    _vkEnumeratePhysicalDevices(_VulkanInstance, &physicalDeviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    _vkEnumeratePhysicalDevices(_VulkanInstance, &physicalDeviceCount, physicalDevices.data());
    std::vector<VkExtensionProperties> extensionProperties;

    int selectedDevicetype = SelectDeviceTypeStart;

    VkPhysicalDeviceProperties physicalDeviceProperties;
    for (uint32_t i = 0; i < physicalDeviceCount; ++i)
    {
        uint32_t count;

        _vkEnumerateDeviceExtensionProperties(physicalDevices[i], nullptr, &count, nullptr);
        extensionProperties.resize(count);
        _vkEnumerateDeviceExtensionProperties(physicalDevices[i], nullptr, &count, extensionProperties.data());

        _vkGetPhysicalDeviceProperties(physicalDevices[i], &physicalDeviceProperties);
        if (!IsVulkanExtensionAvailable(extensionProperties, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;

        if (SelectVulkanPhysicalDeviceType(physicalDeviceProperties.deviceType, selectedDevicetype))
        {
            _VulkanPhysicalDevice = physicalDevices[i];
            if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                break;
        }
    }
    if (_VulkanPhysicalDevice == nullptr)
    {
        _vkDestroyInstance(_VulkanInstance, _VulkanAllocationCallbacks);
        return false;
    }

    return true;
}

bool VulkanHook_t::_CreateImageSampler()
{
    if (_VulkanImageSampler != VK_NULL_HANDLE)
        return true;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.minLod = -1000;
    samplerInfo.maxLod = 1000;
    samplerInfo.maxAnisotropy = 1.0f;
    return _vkCreateSampler(_VulkanDevice, &samplerInfo, _VulkanAllocationCallbacks, &_VulkanImageSampler) == VkResult::VK_SUCCESS;
}

void VulkanHook_t::_DestroyImageSampler()
{
    if (_VulkanImageSampler != VK_NULL_HANDLE)
    {
        _vkDestroySampler(_VulkanDevice, _VulkanImageSampler, _VulkanAllocationCallbacks);
        _VulkanImageSampler = VK_NULL_HANDLE;
    }
}

bool VulkanHook_t::_CreateImageDescriptorSetLayout()
{
    if (_VulkanImageDescriptorSetLayout != VK_NULL_HANDLE)
        return true;

    VkDescriptorSetLayoutBinding binding[1] = {};
    binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[0].descriptorCount = 1;
    binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = binding;
    return _vkCreateDescriptorSetLayout(_VulkanDevice, &info, _VulkanAllocationCallbacks, &_VulkanImageDescriptorSetLayout) == VkResult::VK_SUCCESS;
}

void VulkanHook_t::_DestroyImageDescriptorSetLayout()
{
    if (_VulkanImageDescriptorSetLayout != VK_NULL_HANDLE)
    {
        _vkDestroyDescriptorSetLayout(_VulkanDevice, _VulkanImageDescriptorSetLayout, _VulkanAllocationCallbacks);
        _VulkanImageDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool VulkanHook_t::_CreateImageCommandPool()
{
    if (_VulkanImageCommandPool != VK_NULL_HANDLE)
        return true;

    VkCommandPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = 0;
    info.queueFamilyIndex = _VulkanQueueFamily;
    return _vkCreateCommandPool(_VulkanDevice, &info, _VulkanAllocationCallbacks, &_VulkanImageCommandPool) == VkResult::VK_SUCCESS;
}

void VulkanHook_t::_DestroyImageCommandPool()
{
    if (_VulkanImageCommandPool != VK_NULL_HANDLE)
    {
        _vkDestroyCommandPool(_VulkanDevice, _VulkanImageCommandPool, _VulkanAllocationCallbacks);
        _VulkanImageCommandPool = VK_NULL_HANDLE;
    }
}

bool VulkanHook_t::_CreateImageCommandBuffer()
{
    if (_VulkanImageCommandBuffer != VK_NULL_HANDLE)
        return true;

    VkCommandBufferAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = _VulkanImageCommandPool;
    info.commandBufferCount = 1;
    return _vkAllocateCommandBuffers(_VulkanDevice, &info, &_VulkanImageCommandBuffer) == VkResult::VK_SUCCESS;
}

void VulkanHook_t::_DestroyImageCommandBuffer()
{
    if (_VulkanImageCommandBuffer != VK_NULL_HANDLE)
    {
        _vkFreeCommandBuffers(_VulkanDevice, _VulkanImageCommandPool, 1, &_VulkanImageCommandBuffer);
        _VulkanImageCommandBuffer = VK_NULL_HANDLE;
    }
}

bool VulkanHook_t::_CreateImageDevices()
{
    if (!_CreateImageSampler())
        return false;

    if (!_CreateImageDescriptorSetLayout())
        return false;
    
    if (!_CreateImageCommandPool())
        return false;

    if (!_CreateImageCommandBuffer())
        return false;

    return true;
}

void VulkanHook_t::_DestroyImageDevices()
{
    _DestroyImageCommandBuffer();
    _DestroyImageCommandPool();
    _DestroyImageDescriptorSetLayout();
    _DestroyImageSampler();
}

bool VulkanHook_t::_CreateRenderPass()
{
    if (_VulkanRenderPass != VK_NULL_HANDLE)
        return true;

    VkAttachmentDescription attachment = { };
    attachment.format = _VulkanTargetFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachment = { };
    colorAttachment.attachment = 0;
    colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;

    VkRenderPassCreateInfo info = { };
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;

    return _vkCreateRenderPass(_VulkanDevice, &info, _VulkanAllocationCallbacks, &_VulkanRenderPass) == VkResult::VK_SUCCESS;
}

void VulkanHook_t::_DestroyRenderPass()
{
    if (_VulkanRenderPass != VK_NULL_HANDLE)
    {
        _vkDestroyRenderPass(_VulkanDevice, _VulkanRenderPass, _VulkanAllocationCallbacks);
        _VulkanRenderPass = VK_NULL_HANDLE;
    }
}

uint32_t VulkanHook_t::_GetVulkanMemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits)
{
    VkPhysicalDeviceMemoryProperties prop;
    _vkGetPhysicalDeviceMemoryProperties(_VulkanPhysicalDevice, &prop);
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
    {
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
            return i;
    }
    return 0xFFFFFFFF; // Unable to find memoryType
}

bool VulkanHook_t::_DoesQueueSupportGraphic(VkQueue queue)
{
    for (uint32_t i = 0; i < _VulkanQueueFamilies.size(); ++i)
    {
        const VkQueueFamilyProperties& family = _VulkanQueueFamilies[i];
        if (!(family.queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        for (uint32_t j = 0; j < family.queueCount; ++j)
        {
            VkQueue it = VK_NULL_HANDLE;
            _vkGetDeviceQueue(_VulkanDevice, i, j, &it);

            if (queue == it)
                return true;
        }
    }

    return false;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void VulkanHook_t::_PrepareForOverlay(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    if (_VulkanDevice == nullptr)
        return;

    if (_HookState == OverlayHookState::Removing)
    {
        auto windows = X11Hook_t::Inst()->FindApplicationX11Window(getpid());
        if (windows.empty())
            return;

        _Window = (void*)windows[0];

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        if (!X11Hook_t::Inst()->SetInitialWindowSize((Window)_Window))
            return;

        if (_VulkanQueue == nullptr)
            _vkGetDeviceQueue(_VulkanDevice, _VulkanQueueFamily, 0, &_VulkanQueue);

        if (!_CreateRenderPass())
            return;

        if (_DescriptorsPools.empty() && _AllocDescriptorPool())
            return;

        if (!_CreateImageDevices())
            return;

        if (!_CreateRenderTargets(pPresentInfo->pSwapchains[0]))
            return;

        ImGui_ImplVulkan_LoadFunctions(&VulkanHook_t::_LoadVulkanFunction, this);
        _ImGuiFontDescriptor = _GetFreeDescriptorSet();

        ImGui_ImplVulkan_InitInfo init_info = { };
        init_info.PhysicalDevice = _VulkanPhysicalDevice;
        init_info.Device = _VulkanDevice;
        init_info.QueueFamily = _VulkanQueueFamily;
        init_info.Queue = _VulkanQueue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.FontDescriptorSet = _ImGuiFontDescriptor.DescriptorSet;
        init_info.Subpass = 0;
        init_info.MinImageCount = _Frames.size();
        init_info.ImageCount = _Frames.size();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = _VulkanAllocationCallbacks;
        init_info.UseDynamicRendering = false;

        ImGui_ImplVulkan_Init(&init_info, _VulkanRenderPass);

        _ResetRenderState(OverlayHookState::Ready);
    }

    const bool queueSupportsGraphic = _DoesQueueSupportGraphic(queue);

    for (int i = 0; i < pPresentInfo->swapchainCount; ++i)
    {
        auto& frame = _Frames[pPresentInfo->pImageIndices[i]];

        {
            _vkWaitForFences(_VulkanDevice, 1, &frame.Fence, VK_TRUE, ~0ull);
            _vkResetFences(_VulkanDevice, 1, &frame.Fence);
        }
        {
            _vkResetCommandBuffer(frame.CommandBuffer, 0);

            VkCommandBufferBeginInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            _vkBeginCommandBuffer(frame.CommandBuffer, &info);
        }
        {
            VkRenderPassBeginInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = _VulkanRenderPass;
            info.framebuffer = frame.Framebuffer;
            info.renderArea.extent.width = ImGui::GetIO().DisplaySize.x;
            info.renderArea.extent.height = ImGui::GetIO().DisplaySize.y;

            _vkCmdBeginRenderPass(frame.CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        if (ImGui_ImplVulkan_NewFrame() && !X11Hook_t::Inst()->PrepareForOverlay((Window)_Window))
            return;
        
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot(frame);

        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();

        ImGui::Render();

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.CommandBuffer);

        // Submit command buffer
        _vkCmdEndRenderPass(frame.CommandBuffer);
        _vkEndCommandBuffer(frame.CommandBuffer);

        uint32_t waitSemaphoresCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
        if (waitSemaphoresCount == 0 && !queueSupportsGraphic)
        {
            VkPipelineStageFlags stages_wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            {
                VkSubmitInfo info = { };
                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

                info.pWaitDstStageMask = &stages_wait;

                info.signalSemaphoreCount = 1;
                info.pSignalSemaphores = &frame.RenderCompleteSemaphore;

                _vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE);
            }
            {
                VkSubmitInfo info = { };
                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                info.commandBufferCount = 1;
                info.pCommandBuffers = &frame.CommandBuffer;

                info.pWaitDstStageMask = &stages_wait;
                info.waitSemaphoreCount = 1;
                info.pWaitSemaphores = &frame.RenderCompleteSemaphore;

                info.signalSemaphoreCount = 0;
                info.pSignalSemaphores = &frame.ImageAcquiredSemaphore;

                _vkQueueSubmit(_VulkanQueue, 1, &info, frame.Fence);
            }
        }
        else
        {
            std::vector<VkPipelineStageFlags> stages_wait(waitSemaphoresCount, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            VkSubmitInfo info = { };
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &frame.CommandBuffer;

            info.pWaitDstStageMask = stages_wait.data();
            info.waitSemaphoreCount = waitSemaphoresCount;
            info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;

            info.signalSemaphoreCount = waitSemaphoresCount;
            info.pSignalSemaphores = pPresentInfo->pWaitSemaphores;
            // Vulkan layer validation error, nothing waiting on ImageAcquiredSemaphore :/
            //info.signalSemaphoreCount = 1;
            //info.pSignalSemaphores = &frame.ImageAcquiredSemaphore;

            _vkQueueSubmit(_VulkanQueue, 1, &info, frame.Fence);
        }

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot(frame);
    }
}

void VulkanHook_t::_HandleScreenshot(VulkanFrame_t& frame)
{
    InGameOverlay::ScreenshotData_t screenshotData;
    if (_CaptureScreenshot(frame, screenshotData))
        _SendScreenshot(&screenshotData);
    else
        _SendScreenshot(nullptr);
}

bool VulkanHook_t::_CaptureScreenshot(VulkanFrame_t& frame, ScreenshotData_t& outData)
{
    const int32_t bytesPerPixel = 4; // Assuming 4 bytes per pixel
    const int32_t width = ImGui::GetIO().DisplaySize.x;
    const int32_t height = ImGui::GetIO().DisplaySize.y;
    const size_t rowSize = width * bytesPerPixel;

    bool result = false;

    VkResult vkResult;

    // 1. Create staging image (host-visible)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = _VulkanTargetFormat;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage dstImage = VK_NULL_HANDLE;
    VkDeviceMemory dstMemory = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    VkMemoryRequirements memReqs{};
    VkMemoryAllocateInfo allocInfo{};
    VkPhysicalDeviceMemoryProperties memProps{};
    VkCommandBufferAllocateInfo cmdBufAllocInfo{};
    VkCommandBufferBeginInfo beginInfo{};
    VkImageMemoryBarrier barrier{};
    VkImageCopy imageCopy{};
    VkSubmitInfo submitInfo{};
    VkImageSubresource subresource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout layout{};

    const char* mapped = nullptr;

    vkResult = _vkCreateImage(_VulkanDevice, &imageInfo, nullptr, &dstImage);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    // 2. Allocate memory for staging image
    _vkGetImageMemoryRequirements(_VulkanDevice, dstImage, &memReqs);

    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;

    // Find host-visible memory type
    _vkGetPhysicalDeviceMemoryProperties(_VulkanPhysicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
        {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    vkResult = _vkAllocateMemory(_VulkanDevice, &allocInfo, nullptr, &dstMemory);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    vkResult = _vkBindImageMemory(_VulkanDevice, dstImage, dstMemory, 0);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    // 3. Command buffer: Copy from srcImage to dstImage
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandPool = frame.CommandPool;
    cmdBufAllocInfo.commandBufferCount = 1;

    vkResult = _vkAllocateCommandBuffers(_VulkanDevice, &cmdBufAllocInfo, &cmdBuffer);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResult = _vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    // Transition dst image to transfer dst layout
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = dstImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    _vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy from src (swapchain) to dst (linear)
    imageCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopy.srcSubresource.layerCount = 1;
    imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopy.dstSubresource.layerCount = 1;
    imageCopy.extent.width = width;
    imageCopy.extent.height = height;
    imageCopy.extent.depth = 1;

    _vkCmdCopyImage(cmdBuffer,
        frame.BackBuffer, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        dstImage, VK_IMAGE_LAYOUT_GENERAL,
        1, &imageCopy);

    _vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    _vkQueueSubmit(_VulkanQueue, 1, &submitInfo, VK_NULL_HANDLE);
    _vkQueueWaitIdle(_VulkanQueue);

    // 4. Map memory
    _vkGetImageSubresourceLayout(_VulkanDevice, dstImage, &subresource, &layout);

    void* data;
    vkResult = _vkMapMemory(_VulkanDevice, dstMemory, 0, VK_WHOLE_SIZE, 0, &data);
    if (vkResult != VK_SUCCESS)
        goto cleanup;

    mapped = static_cast<const char*>(data);
    outData.Buffer.resize(rowSize * height);

    for (uint32_t y = 0; y < height; ++y)
        memcpy(outData.Buffer.data() + y * rowSize, mapped + layout.offset + layout.rowPitch * y, rowSize);

    outData.Width = width;
    outData.Height = height;
    outData.Format = RendererFormatToScreenshotFormat(_VulkanTargetFormat);

    _vkUnmapMemory(_VulkanDevice, dstMemory);

    result = true;

cleanup:
    if (dstImage != VK_NULL_HANDLE)
        _vkDestroyImage(_VulkanDevice, dstImage, nullptr);

    if (dstMemory != VK_NULL_HANDLE)
        _vkFreeMemory(_VulkanDevice, dstMemory, nullptr);

    if (cmdBuffer != VK_NULL_HANDLE)
        _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &cmdBuffer);

    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    INGAMEOVERLAY_INFO("vkAcquireNextImageKHR");
    auto inst = VulkanHook_t::Inst();

    inst->_VulkanDevice = device;
    return inst->_VkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex)
{
    INGAMEOVERLAY_INFO("vkAcquireNextImage2KHR");
    auto inst = VulkanHook_t::Inst();

    inst->_VulkanDevice = device;
    return inst->_VkAcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    INGAMEOVERLAY_INFO("vkQueuePresentKHR");
    auto inst = VulkanHook_t::Inst();

    // Send VK_SUBOPTIMAL_KHR and see if the game recreates its swapchain, so we can get the rendering color space :p
    if (!inst->_SentOutOfDate)
    {
        inst->_SentOutOfDate = true;
        return VkResult::VK_SUBOPTIMAL_KHR;
    }

    inst->_PrepareForOverlay(queue, pPresentInfo);
    return inst->_VkQueuePresentKHR(queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    INGAMEOVERLAY_INFO("vkCreateSwapchainKHR");
    auto inst = VulkanHook_t::Inst();
    auto createRenderTargets = false;

    if (inst->_VulkanDevice == device && inst->_HookState != OverlayHookState::Removing)
    {
        createRenderTargets = !inst->_Frames.empty();
        inst->_ResetRenderState(OverlayHookState::Reset);
        inst->_DestroyRenderTargets();
    }
    inst->_SentOutOfDate = true;
    inst->_VulkanTargetFormat = pCreateInfo->imageFormat;
    auto res = inst->_VkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (inst->_VulkanDevice == device && res == VkResult::VK_SUCCESS && createRenderTargets)
    {
        inst->_CreateRenderTargets(*pSwapchain);
        inst->_ResetRenderState(OverlayHookState::Ready);
    }
    return res;
}

VKAPI_ATTR void VKAPI_CALL VulkanHook_t::_MyVkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    INGAMEOVERLAY_INFO("vkDestroyDevice");
    auto inst = VulkanHook_t::Inst();

    if (inst->_VulkanDevice == device)
        inst->_ResetRenderState(OverlayHookState::Removing);

    inst->_VkDestroyDevice(device, pAllocator);
}

VulkanHook_t::VulkanHook_t():
    _Hooked(false),
    _X11Hooked(false),
    _Window(nullptr),
    _SentOutOfDate(false),
    _HookState(OverlayHookState::Removing),
    _VulkanLoader(nullptr),
    _VulkanAllocationCallbacks(nullptr),
    _VulkanInstance(VK_NULL_HANDLE),
    _VulkanPhysicalDevice(VK_NULL_HANDLE),
    _VulkanQueueFamily(uint32_t(-1)),
    _VulkanImageCommandPool(VK_NULL_HANDLE),
    _VulkanImageCommandBuffer(VK_NULL_HANDLE),
    _VulkanImageSampler(VK_NULL_HANDLE),
    _VulkanImageDescriptorSetLayout(VK_NULL_HANDLE),
    _VulkanRenderPass(VK_NULL_HANDLE),
    _VulkanTargetFormat(VK_FORMAT_R8G8B8A8_UNORM),
    _VulkanDevice(VK_NULL_HANDLE),
    _VulkanQueue(VK_NULL_HANDLE),
    _ImGuiFontAtlas(nullptr),

    _VkAcquireNextImageKHR(nullptr),
    _VkAcquireNextImage2KHR(nullptr),
    _VkQueuePresentKHR(nullptr),
    _VkCreateSwapchainKHR(nullptr),
    _VkDestroyDevice(nullptr),

    _vkCreateInstance(nullptr),
    _vkDestroyInstance(nullptr),
    _vkGetInstanceProcAddr(nullptr),
    _vkDeviceWaitIdle(nullptr),
    _vkGetDeviceProcAddr(nullptr),
    _vkGetDeviceQueue(nullptr),
    _vkQueueSubmit(nullptr),
    _vkQueueWaitIdle(nullptr),
    _vkCreateRenderPass(nullptr),
    _vkCmdBeginRenderPass(nullptr),
    _vkCmdEndRenderPass(nullptr),
    _vkDestroyRenderPass(nullptr),
    _vkCmdCopyImage(nullptr),
    _vkGetImageSubresourceLayout(nullptr),
    _vkCreateSemaphore(nullptr),
    _vkDestroySemaphore(nullptr),
    _vkCreateBuffer(nullptr),
    _vkDestroyBuffer(nullptr),
    _vkMapMemory(nullptr),
    _vkUnmapMemory(nullptr),
    _vkFlushMappedMemoryRanges(nullptr),
    _vkBindBufferMemory(nullptr),
    _vkCmdCopyBufferToImage(nullptr),
    _vkBindImageMemory(nullptr),
    _vkCreateCommandPool(nullptr),
    _vkResetCommandPool(nullptr),
    _vkDestroyCommandPool(nullptr),
    _vkCreateImageView(nullptr),
    _vkDestroyImageView(nullptr),
    _vkCreateSampler(nullptr),
    _vkDestroySampler(nullptr),
    _vkCreateImage(nullptr),
    _vkDestroyImage(nullptr),
    _vkAllocateMemory(nullptr),
    _vkFreeMemory(nullptr),
    _vkCmdPipelineBarrier(nullptr),
    _vkAllocateCommandBuffers(nullptr),
    _vkBeginCommandBuffer(nullptr),
    _vkResetCommandBuffer(nullptr),
    _vkEndCommandBuffer(nullptr),
    _vkFreeCommandBuffers(nullptr),
    _vkCreateFramebuffer(nullptr),
    _vkDestroyFramebuffer(nullptr),
    _vkCreateFence(nullptr),
    _vkWaitForFences(nullptr),
    _vkResetFences(nullptr),
    _vkDestroyFence(nullptr),
    _vkCreateDescriptorPool(nullptr),
    _vkDestroyDescriptorPool(nullptr),
    _vkCreateDescriptorSetLayout(nullptr),
    _vkDestroyDescriptorSetLayout(nullptr),
    _vkAllocateDescriptorSets(nullptr),
    _vkUpdateDescriptorSets(nullptr),
    _vkFreeDescriptorSets(nullptr),
    _vkGetBufferMemoryRequirements(nullptr),
    _vkGetImageMemoryRequirements(nullptr),
    _vkEnumeratePhysicalDevices(nullptr),
    _vkGetPhysicalDeviceSurfaceFormatsKHR(nullptr),
    _vkGetPhysicalDeviceProperties(nullptr),
    _vkGetPhysicalDeviceQueueFamilyProperties(nullptr),
    _vkGetPhysicalDeviceMemoryProperties(nullptr),
    _vkEnumerateDeviceExtensionProperties(nullptr)
{
}

VulkanHook_t::~VulkanHook_t()
{
    INGAMEOVERLAY_INFO("VulkanHook_t Hook removed");

    if (_X11Hooked)
        delete X11Hook_t::Inst();

    _Instance->UnhookAll();
    _Instance = nullptr;
}

VulkanHook_t* VulkanHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new VulkanHook_t;

    return _Instance;
}

const char* VulkanHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t VulkanHook_t::GetRendererHookType() const
{
    return RendererHookType_t::Vulkan;
}

void VulkanHook_t::LoadFunctions(
    std::function<void* (const char*)> vkLoader,
    decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR,
    decltype(::vkAcquireNextImage2KHR)* vkAcquireNextImage2KHR,
    decltype(::vkQueuePresentKHR)* vkQueuePresentKHR,
    decltype(::vkCreateSwapchainKHR)* vkCreateSwapchainKHR,
    decltype(::vkDestroyDevice)* vkDestroyDevice)
{
    _VulkanLoader = std::move(vkLoader);

    _VkAcquireNextImageKHR = vkAcquireNextImageKHR;
    _VkAcquireNextImage2KHR = vkAcquireNextImage2KHR;
    _VkQueuePresentKHR = vkQueuePresentKHR;
    _VkCreateSwapchainKHR = vkCreateSwapchainKHR;
    _VkDestroyDevice = vkDestroyDevice;
}

std::weak_ptr<uint64_t> VulkanHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    struct VulkanImage_t
    {
        ImTextureID ImageId = 0;
        VkDeviceMemory VulkanImageMemory = VK_NULL_HANDLE;
        VulkanDescriptorSet_t ImageDescriptorId;
        VkImage VulkanImage = VK_NULL_HANDLE;
        VkImageView VulkanImageView = VK_NULL_HANDLE;
    };

    std::shared_ptr<uint64_t> image;
    VkResult result;
    VkImage vulkanImage = VK_NULL_HANDLE;
    VkImageView vulkanImageView = VK_NULL_HANDLE;
    VkDeviceMemory vulkanImageMemory = VK_NULL_HANDLE;
    VulkanDescriptorSet_t vulkanImageDescriptor;
    VkDeviceMemory uploadBufferMemory = VK_NULL_HANDLE;
    VkBuffer uploadBuffer = VK_NULL_HANDLE;

    VkDeviceSize uploadSize = width * height * 4 * sizeof(char);

    VulkanImage_t* vulkanRendererImage = nullptr;

    // Start command buffer 
    {
        result = _vkResetCommandPool(_VulkanDevice, _VulkanImageCommandPool, 0);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = _vkBeginCommandBuffer(_VulkanImageCommandBuffer, &beginInfo);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;
    }

    // Create the Image:
    {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        result = _vkCreateImage(_VulkanDevice, &info, _VulkanAllocationCallbacks, &vulkanImage);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        VkMemoryRequirements req;
        _vkGetImageMemoryRequirements(_VulkanDevice, vulkanImage, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = _GetVulkanMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, req.memoryTypeBits);
        result = _vkAllocateMemory(_VulkanDevice, &alloc_info, _VulkanAllocationCallbacks, &vulkanImageMemory);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        result = _vkBindImageMemory(_VulkanDevice, vulkanImage, vulkanImageMemory, 0);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;
    }

    // Create the Image View:
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = vulkanImage;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        result = _vkCreateImageView(_VulkanDevice, &info, _VulkanAllocationCallbacks, &vulkanImageView);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;
    }

    // Create the Descriptor Set:
    vulkanImageDescriptor = _GetFreeDescriptorSet();
    if (vulkanImageDescriptor.DescriptorPoolId == VulkanDescriptorSet_t::InvalidDescriptorPoolId)
        goto OnErrorCreateImage;

    _CreateImageTexture(vulkanImageDescriptor.DescriptorSet, vulkanImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create the Upload Buffer:
    {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = uploadSize;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        result = _vkCreateBuffer(_VulkanDevice, &buffer_info, _VulkanAllocationCallbacks, &uploadBuffer);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        VkMemoryRequirements req;
        _vkGetBufferMemoryRequirements(_VulkanDevice, uploadBuffer, &req);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = _GetVulkanMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
        result = _vkAllocateMemory(_VulkanDevice, &alloc_info, _VulkanAllocationCallbacks, &uploadBufferMemory);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        result = _vkBindBufferMemory(_VulkanDevice, uploadBuffer, uploadBufferMemory, 0);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;
    }

    // Upload to Buffer:
    {
        char* map = nullptr;
        result = _vkMapMemory(_VulkanDevice, uploadBufferMemory, 0, uploadSize, 0, (void**)(&map));
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        memcpy(map, image_data, uploadSize);
        VkMappedMemoryRange range[1] = {};
        range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range[0].memory = uploadBufferMemory;
        range[0].size = uploadSize;
        result = _vkFlushMappedMemoryRanges(_VulkanDevice, 1, range);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        _vkUnmapMemory(_VulkanDevice, uploadBufferMemory);
    }

    // Copy to Image:
    {
        VkImageMemoryBarrier copyBarrier[1] = {};
        copyBarrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copyBarrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        copyBarrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarrier[0].image = vulkanImage;
        copyBarrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyBarrier[0].subresourceRange.levelCount = 1;
        copyBarrier[0].subresourceRange.layerCount = 1;
        _vkCmdPipelineBarrier(_VulkanImageCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, copyBarrier);

        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = width;
        region.imageExtent.height = height;
        region.imageExtent.depth = 1;
        _vkCmdCopyBufferToImage(_VulkanImageCommandBuffer, uploadBuffer, vulkanImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier useBarrier[1] = {};
        useBarrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        useBarrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        useBarrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        useBarrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        useBarrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        useBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        useBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        useBarrier[0].image = vulkanImage;
        useBarrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        useBarrier[0].subresourceRange.levelCount = 1;
        useBarrier[0].subresourceRange.layerCount = 1;
        _vkCmdPipelineBarrier(_VulkanImageCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, useBarrier);
    }

    // End command buffer 
    {
        VkSubmitInfo endInfo = {};
        endInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        endInfo.commandBufferCount = 1;
        endInfo.pCommandBuffers = &_VulkanImageCommandBuffer;
        result = _vkEndCommandBuffer(_VulkanImageCommandBuffer);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        result = _vkQueueSubmit(_VulkanQueue, 1, &endInfo, VK_NULL_HANDLE);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;


        result = _vkQueueWaitIdle(_VulkanQueue);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;


        // TODO: Reuse buffer instead of create one per image
        _vkDestroyBuffer(_VulkanDevice, uploadBuffer, _VulkanAllocationCallbacks);
        _vkFreeMemory(_VulkanDevice, uploadBufferMemory, _VulkanAllocationCallbacks);
    }

    vulkanRendererImage = new VulkanImage_t;
    vulkanRendererImage->ImageId = (ImTextureID)vulkanImageDescriptor.DescriptorSet;
    vulkanRendererImage->VulkanImageMemory = vulkanImageMemory;
    vulkanRendererImage->ImageDescriptorId = vulkanImageDescriptor;
    vulkanRendererImage->VulkanImage = vulkanImage;
    vulkanRendererImage->VulkanImageView = vulkanImageView;

    image = std::shared_ptr<uint64_t>((uint64_t*)vulkanRendererImage, [this](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            auto vulkanImage = reinterpret_cast<VulkanImage_t*>(handle);

            // TODO: Might need to wait on the last command buffer?
            // Message: Validation Error: [ VUID-vkFreeDescriptorSets-pDescriptorSets-00309 ] Object 0: handle = 0x45d6d1000000004c, type = VK_OBJECT_TYPE_DESCRIPTOR_SET;
            // MessageID = 0xbfce1114
            // vkFreeDescriptorSets(): pDescriptorSets[0] VkDescriptorSet 0x45d6d1000000004c[] is in use by VkCommandBuffer 0x1b0f501bff0[].
            // The Vulkan spec states: All submitted commands that refer to any element of pDescriptorSets must have completed execution (https://vulkan.lunarg.com/doc/view/1.3.275.0/windows/1.3-extensions/vkspec.html#VUID-vkFreeDescriptorSets-pDescriptorSets-00309)

            _ReleaseDescriptor(vulkanImage->ImageDescriptorId);
            _vkDestroyImageView(_VulkanDevice, vulkanImage->VulkanImageView, _VulkanAllocationCallbacks);
            _vkFreeMemory(_VulkanDevice, vulkanImage->VulkanImageMemory, _VulkanAllocationCallbacks);
            _vkDestroyImage(_VulkanDevice, vulkanImage->VulkanImage, _VulkanAllocationCallbacks);

            delete vulkanImage;
        }
    });

    _ImageResources.emplace(image);

    return image;

OnErrorCreateImage:
    if (vulkanImageDescriptor.DescriptorSet != VK_NULL_HANDLE) _ReleaseDescriptor(vulkanImageDescriptor);
    if (uploadBuffer                        != VK_NULL_HANDLE) _vkDestroyBuffer   (_VulkanDevice, uploadBuffer      , _VulkanAllocationCallbacks);
    if (uploadBufferMemory                  != VK_NULL_HANDLE) _vkFreeMemory      (_VulkanDevice, uploadBufferMemory, _VulkanAllocationCallbacks);
    if (vulkanImageView                     != VK_NULL_HANDLE) _vkDestroyImageView(_VulkanDevice, vulkanImageView   , _VulkanAllocationCallbacks);
    if (vulkanImageMemory                   != VK_NULL_HANDLE) _vkFreeMemory      (_VulkanDevice, vulkanImageMemory , _VulkanAllocationCallbacks);
    if (vulkanImage                         != VK_NULL_HANDLE) _vkDestroyImage    (_VulkanDevice, vulkanImage       , _VulkanAllocationCallbacks);

    return std::shared_ptr<uint64_t>(nullptr);
}

void VulkanHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
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
