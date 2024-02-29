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

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&VulkanHook_t::_My##NAME))) { \
    SPDLOG_ERROR("Failed to hook {}", #NAME);\
    return false;\
} } while(0)

VulkanHook_t* VulkanHook_t::_Instance = nullptr;

static bool _IsExtensionAvailable(const std::vector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;

    return false;
}

static inline VkResult _CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return err;

    SPDLOG_ERROR("[vulkan] Error: VkResult = {}", (int)err);
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

bool VulkanHook_t::StartHook(std::function<void()> key_combination_callback, std::set<ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_VkAcquireNextImageKHR == nullptr || _VkQueuePresentKHR == nullptr || _VkDestroyDevice  == nullptr || _VulkanFunctionLoader == nullptr)
        {
            SPDLOG_WARN("Failed to hook Vulkan: Rendering functions missing.");
            return false;
        }

        if (!WindowsHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(VkAcquireNextImageKHR);
        TRY_HOOK_FUNCTION(VkQueuePresentKHR);
        TRY_HOOK_FUNCTION(VkDestroyDevice);
        EndHook();

        SPDLOG_INFO("Hooked Vulkan");
        _Hooked = true;
        _ImGuiFontAtlas = imgui_font_atlas;
    }
    return true;
}

void VulkanHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideAppInputs(hide);
    }
}

void VulkanHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        WindowsHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool VulkanHook_t::IsStarted()
{
    return _Hooked;
}

bool VulkanHook_t::_AllocDescriptorPool()
{    
    VkDescriptorPool vulkanDescriptorPool = nullptr;

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
    if (_vkCreateDescriptorPool(_VulkanDevice, &descriptorPoolCreateInfo, nullptr, &vulkanDescriptorPool) != VkResult::VK_SUCCESS || vulkanDescriptorPool == nullptr)
        return false;

    
    _DescriptorsPools.emplace_back();
    auto& descriptorPool = *_DescriptorsPools.rbegin();
    descriptorPool.DescriptorPool = vulkanDescriptorPool;

    return true;
}

VulkanHook_t::VulkanDescriptorSet_t VulkanHook_t::_GetFreeDescriptorSetFromPool(uint32_t poolIndex)
{
    auto& descriptorsPool = _DescriptorsPools[poolIndex];
    VkDescriptorSet vulkanDescriptorSet = nullptr;
    VulkanDescriptorSet_t descriptorSet;

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptorsPool.DescriptorPool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &_VulkanDescriptorSetLayout;
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

void VulkanHook_t::_ReleaseDescriptor(uint32_t id, VkDescriptorSet descriptorSet)
{
    auto& pool = _DescriptorsPools[GetImageDescriptorPool(id)];

    _vkFreeDescriptorSets(_VulkanDevice, pool.DescriptorPool, 1, &descriptorSet);
    --pool.UsedDescriptors;
}

void VulkanHook_t::_CreateImageTexture(VkDescriptorSet descriptorSet, VkImageView imageView, VkImageLayout imageLayout)
{
    VkDescriptorImageInfo desc_image[1] = {};
    desc_image[0].sampler = _VulkanImageSampler;
    desc_image[0].imageView = imageView;
    desc_image[0].imageLayout = imageLayout;
    VkWriteDescriptorSet write_desc[1] = {};
    write_desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_desc[0].dstSet = descriptorSet;
    write_desc[0].descriptorCount = 1;
    write_desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_desc[0].pImageInfo = desc_image;
    _vkUpdateDescriptorSets(_VulkanDevice, 1, write_desc, 0, nullptr);
}

void VulkanHook_t::_ResetRenderState(OverlayHookState state)
{
    if (_Initialized)
    {
        OverlayHookReady(state);

        _DestroyFrames();

        if (state == OverlayHookState::Removing)
        {
            ImGui_ImplVulkan_Shutdown();

            WindowsHook_t::Inst()->ResetRenderState(state);
            _FreeVulkanRessources();
            _Initialized = false;
        }
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

bool VulkanHook_t::_FindApplicationHWND()
{
    struct
    {
        DWORD pid;
        std::vector<HWND> windows;
    } windowParams{
        GetCurrentProcessId()
    };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        if (!IsWindowVisible(hwnd) && !IsIconic(hwnd))
            return TRUE;

        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);

        auto params = reinterpret_cast<decltype(windowParams)*>(lParam);

        if (processId == params->pid)
            params->windows.emplace_back(hwnd);

        return TRUE;
    }, reinterpret_cast<LPARAM>(&windowParams));

    if (windowParams.windows.empty())
        return false;

    _MainWindow = windowParams.windows[0];
    return true;
}

void VulkanHook_t::_FreeVulkanRessources()
{
    if (_VulkanInstance == nullptr)
        return;

    if (_VulkanDevice != nullptr)
    {
        _DestroyFrames();

        if (_VulkanImageSampler != nullptr)
        {
            _vkDestroySampler(_VulkanDevice, _VulkanImageSampler, nullptr);
            _VulkanImageSampler = nullptr;
        }

        if (_VulkanImageCommandBuffer != nullptr)
        {
            _vkFreeCommandBuffers(_VulkanDevice, _VulkanImageCommandPool, 1, &_VulkanImageCommandBuffer);
            _VulkanImageCommandBuffer = nullptr;
        }

        if (_VulkanImageCommandPool != nullptr)
        {
            _vkDestroyCommandPool(_VulkanDevice, _VulkanImageCommandPool, nullptr);
            _VulkanImageCommandPool = nullptr;
        }

        if (_VulkanDescriptorSetLayout != nullptr)
        {
            _vkDestroyDescriptorSetLayout(_VulkanDevice, _VulkanDescriptorSetLayout, nullptr);
            _VulkanDescriptorSetLayout = nullptr;
        }

        _ImageResources.clear();
        for (auto& pool : _DescriptorsPools)
        {
            _vkDestroyDescriptorPool(_VulkanDevice, pool.DescriptorPool, nullptr);
        }
        _DescriptorsPools.clear();

        if (_VulkanRenderPass != nullptr)
        {
            _vkDestroyRenderPass(_VulkanDevice, _VulkanRenderPass, nullptr);
            _VulkanRenderPass = nullptr;
        }

        _VulkanDevice = nullptr;
    }

    _vkDestroyInstance(_VulkanInstance, nullptr);
    _VulkanInstance = nullptr;
    _VulkanPhysicalDevice = nullptr;
}

bool VulkanHook_t::_CreateVulkanInstance()
{
    VkInstanceCreateInfo instanceInfos{};
    uint32_t propertiesCount;
    std::vector<VkExtensionProperties> properties;
    std::vector<const char*> instanceExtensions;

#define LOAD_VULKAN_FUNCTION(NAME) if (_##NAME == nullptr) _##NAME = (decltype(_##NAME))_VulkanFunctionLoader(#NAME)
    LOAD_VULKAN_FUNCTION(vkCreateInstance);
    LOAD_VULKAN_FUNCTION(vkDestroyInstance);
    LOAD_VULKAN_FUNCTION(vkGetInstanceProcAddr);
    LOAD_VULKAN_FUNCTION(vkEnumerateInstanceExtensionProperties);
#undef LOAD_VULKAN_FUNCTION

    _vkEnumerateInstanceExtensionProperties(nullptr, &propertiesCount, nullptr);
    properties.resize(propertiesCount);
    auto err = _vkEnumerateInstanceExtensionProperties(nullptr, &propertiesCount, properties.data());

    for (auto const& extension : { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME })
    {
        if (_IsExtensionAvailable(properties, extension))
            instanceExtensions.emplace_back(extension);
    }

    // Create Vulkan Instance
    instanceInfos.enabledExtensionCount = (uint32_t)instanceExtensions.size();
    instanceInfos.ppEnabledExtensionNames = instanceExtensions.data();

    instanceInfos.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    bool result = _vkCreateInstance(&instanceInfos, nullptr, &_VulkanInstance) == VkResult::VK_SUCCESS && _VulkanInstance != nullptr;

    if (result)
    {
#define LOAD_VULKAN_FUNCTION(NAME) if (_##NAME == nullptr) _##NAME = (decltype(_##NAME))_vkGetInstanceProcAddr(_VulkanInstance, #NAME)
        LOAD_VULKAN_FUNCTION(vkQueueSubmit);
        LOAD_VULKAN_FUNCTION(vkQueueWaitIdle);
        LOAD_VULKAN_FUNCTION(vkGetDeviceQueue);
        LOAD_VULKAN_FUNCTION(vkCreateRenderPass);
        LOAD_VULKAN_FUNCTION(vkDestroyRenderPass);
        LOAD_VULKAN_FUNCTION(vkCmdBeginRenderPass);
        LOAD_VULKAN_FUNCTION(vkCmdEndRenderPass);
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
        LOAD_VULKAN_FUNCTION(vkEnumerateDeviceExtensionProperties);
        LOAD_VULKAN_FUNCTION(vkEnumeratePhysicalDevices);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceProperties);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceMemoryProperties);
        LOAD_VULKAN_FUNCTION(vkGetSwapchainImagesKHR);
#undef LOAD_VULKAN_FUNCTION
    }

    return result;
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
    std::vector<VkPhysicalDevice> physicalDevices;
    std::vector<uint32_t> preferedPhysicalDevicesIndex;
    std::vector<VkExtensionProperties> extensionProperties;
    std::vector<VkQueueFamilyProperties> queuesFamilyProperties;
    VkPhysicalDeviceProperties physicalDevicesProperties;
    VkQueue vulkanQueue;
    VkDeviceCreateInfo deviceCreateInfo{};
    int queueFamilyIndex = -1;
    uint32_t count = 0;

    _vkEnumeratePhysicalDevices(_VulkanInstance, &count, nullptr);
    physicalDevices.resize(count);
    _vkEnumeratePhysicalDevices(_VulkanInstance, &count, physicalDevices.data());

    for (uint32_t i = 0; i < physicalDevices.size();)
    {
        _vkGetPhysicalDeviceProperties(physicalDevices[i], &physicalDevicesProperties);
        if (physicalDevicesProperties.deviceType != VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            physicalDevicesProperties.deviceType != VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            physicalDevices.erase(physicalDevices.begin() + i);
            continue;
        }

        if (physicalDevicesProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            preferedPhysicalDevicesIndex.insert(preferedPhysicalDevicesIndex.begin(), i);
        else
            preferedPhysicalDevicesIndex.emplace_back(i);

        ++i;
    }

    for (auto deviceIndex : preferedPhysicalDevicesIndex)
    {
        _vkEnumerateDeviceExtensionProperties(physicalDevices[deviceIndex], nullptr, &count, nullptr);
        extensionProperties.resize(count);
        _vkEnumerateDeviceExtensionProperties(physicalDevices[deviceIndex], nullptr, &count, extensionProperties.data());
    
        for (auto& extension : extensionProperties)
        {
            if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0)
                continue;
    
            queueFamilyIndex = _GetPhysicalDeviceFirstGraphicsQueue(physicalDevices[deviceIndex]);
            if (queueFamilyIndex < 0)
                continue;

            _vkGetDeviceQueue(_VulkanDevice, queueFamilyIndex, 0, &vulkanQueue);
            
            _QueueFamilyIndex = queueFamilyIndex;
            _VulkanPhysicalDevice = physicalDevices[deviceIndex];
            _VulkanQueue = vulkanQueue;
            return true;
        }
    }

    return false;
}

bool VulkanHook_t::_CreateRenderPass()
{
    VkRenderPass vulkanRenderPass = nullptr;
    VkAttachmentDescription attachment = {};
    // TODO: Find a way to use the correct format
    attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    if (_vkCreateRenderPass(_VulkanDevice, &info, nullptr, &vulkanRenderPass) != VkResult::VK_SUCCESS || vulkanRenderPass == nullptr)
        return false;

    _VulkanRenderPass = vulkanRenderPass;
    return true;
}

bool VulkanHook_t::_CreateRenderTargets(VkSwapchainKHR swapChain)
{
    uint32_t backBufferCount;
    std::vector<VkImage> backBuffers;
    _vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &backBufferCount, nullptr);
    backBuffers.resize(backBufferCount);
    _vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &backBufferCount, backBuffers.data());

    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = 0;
    commandPoolCreateInfo.queueFamilyIndex = _QueueFamilyIndex;

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkImageViewCreateInfo imageViewCreateInfo = {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    // TODO: Find a way to use the correct format
    imageViewCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    VkImageSubresourceRange image_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    imageViewCreateInfo.subresourceRange = image_range;

    VkImageView attachment[1];
    VkFramebufferCreateInfo framebufferCreateInfo = {};
    framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferCreateInfo.renderPass = _VulkanRenderPass;
    framebufferCreateInfo.attachmentCount = 1;
    framebufferCreateInfo.pAttachments = attachment;
    framebufferCreateInfo.width = ImGui::GetIO().DisplaySize.x;
    framebufferCreateInfo.height = ImGui::GetIO().DisplaySize.y;
    framebufferCreateInfo.layers = 1;

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    _Frames.resize(backBufferCount);
    for (uint32_t i = 0; i < backBufferCount; i++)
    {
        auto& frame = _Frames[i];

        if (_vkCreateCommandPool(_VulkanDevice, &commandPoolCreateInfo, nullptr, &frame.CommandPool) != VkResult::VK_SUCCESS || frame.CommandPool == nullptr)
            return false;

        commandBufferAllocateInfo.commandPool = frame.CommandPool;
        if (_vkAllocateCommandBuffers(_VulkanDevice, &commandBufferAllocateInfo, &frame.CommandBuffer) != VkResult::VK_SUCCESS || frame.CommandBuffer == nullptr)
        {
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
            return false;
        }

        imageViewCreateInfo.image = backBuffers[i];
        if (_vkCreateImageView(_VulkanDevice, &imageViewCreateInfo, nullptr, &frame.RenderTarget) != VkResult::VK_SUCCESS || frame.RenderTarget == nullptr)
        {
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
            return false;
        }
        
        attachment[0] = frame.RenderTarget;
        if (_vkCreateFramebuffer(_VulkanDevice, &framebufferCreateInfo, nullptr, &frame.Framebuffer) != VkResult::VK_SUCCESS || frame.Framebuffer == nullptr)
        {
            _vkDestroyImageView(_VulkanDevice, frame.RenderTarget, nullptr);
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
            return false;
        }

        if (_vkCreateSemaphore(_VulkanDevice, &semaphoreCreateInfo, nullptr, &frame.Semaphore) != VkResult::VK_SUCCESS || frame.Semaphore == nullptr)
        {
            _vkDestroyFramebuffer(_VulkanDevice, frame.Framebuffer, nullptr);
            _vkDestroyImageView(_VulkanDevice, frame.RenderTarget, nullptr);
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
            return false;
        }

        if (_vkCreateFence(_VulkanDevice, &fenceCreateInfo, nullptr, &frame.Fence) != VkResult::VK_SUCCESS || frame.Fence == nullptr)
        {
            _vkDestroySemaphore(_VulkanDevice, frame.Semaphore, nullptr);
            _vkDestroyFramebuffer(_VulkanDevice, frame.Framebuffer, nullptr);
            _vkDestroyImageView(_VulkanDevice, frame.RenderTarget, nullptr);
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
            return false;
        }

        frame.BackBuffer = backBuffers[i];
    }
    return true;
}

bool VulkanHook_t::_SetupVulkanRenderer()
{
    if (!_CreateVulkanInstance())
        return false;

    if (!_GetPhysicalDevice())
        return false;

    if (!_AllocDescriptorPool())
        return false;

    if (!_CreateImageObjects())
        return false;

    if (!_CreateRenderPass())
        return false;

    return true;
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

bool VulkanHook_t::_CreateImageObjects()
{
    VkResult result;
    VkSampler vulkanSampler = nullptr;
    VkCommandPool vulkanCommandPool = nullptr;
    VkCommandBuffer vulkanCommandBuffer = nullptr;
    VkDescriptorSetLayout vulkanDescriptorSetLayout = nullptr;

    // Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
    VkSamplerCreateInfo samplerCreateInfo = {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.minLod = -1000;
    samplerCreateInfo.maxLod = 1000;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    result = _vkCreateSampler(_VulkanDevice, &samplerCreateInfo, nullptr, &vulkanSampler);
    _CheckVkResult(result);
    if (result != VkResult::VK_SUCCESS)
        return false;

    // Create command pool/buffer
    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = 0;
    commandPoolCreateInfo.queueFamilyIndex = _QueueFamilyIndex;
    result = _vkCreateCommandPool(_VulkanDevice, &commandPoolCreateInfo, nullptr, &vulkanCommandPool);
    _CheckVkResult(result);
    if (result != VkResult::VK_SUCCESS)
    {
        _vkDestroySampler(_VulkanDevice, vulkanSampler, nullptr);
        return false;
    }

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = vulkanCommandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;
    result = _vkAllocateCommandBuffers(_VulkanDevice, &commandBufferAllocateInfo, &vulkanCommandBuffer);
    _CheckVkResult(result);
    if (result != VkResult::VK_SUCCESS)
    {
        _vkDestroySampler(_VulkanDevice, vulkanSampler, nullptr);
        _vkDestroyCommandPool(_VulkanDevice, vulkanCommandPool, nullptr);
        return false;
    }

    VkDescriptorSetLayoutBinding binding[1] = {};
    binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[0].descriptorCount = 1;
    binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = binding;
    result = _vkCreateDescriptorSetLayout(_VulkanDevice, &info, nullptr, &vulkanDescriptorSetLayout);
    _CheckVkResult(result);
    if (result != VkResult::VK_SUCCESS)
    {
        _vkDestroySampler(_VulkanDevice, vulkanSampler, nullptr);
        _vkDestroyCommandPool(_VulkanDevice, vulkanCommandPool, nullptr);
        return false;
    }

    _VulkanImageSampler = vulkanSampler;
    _VulkanImageCommandPool = vulkanCommandPool;
    _VulkanImageCommandBuffer = vulkanCommandBuffer;
    _VulkanDescriptorSetLayout = vulkanDescriptorSetLayout;
    return true;
}

void VulkanHook_t::_DestroyFrames()
{
    for (auto& frame : _Frames)
    {
        if (frame.Fence != nullptr)
            _vkDestroyFence(_VulkanDevice, frame.Fence, nullptr);

        if (frame.Semaphore != nullptr)
            _vkDestroySemaphore(_VulkanDevice, frame.Semaphore, nullptr);

        if (frame.Framebuffer != nullptr)
            _vkDestroyFramebuffer(_VulkanDevice, frame.Framebuffer, nullptr);

        if (frame.RenderTarget != nullptr)
            _vkDestroyImageView(_VulkanDevice, frame.RenderTarget, nullptr);

        if (frame.CommandBuffer != nullptr)
            _vkFreeCommandBuffers(_VulkanDevice, frame.CommandPool, 1, &frame.CommandBuffer);

        if (frame.CommandPool != nullptr)
            _vkDestroyCommandPool(_VulkanDevice, frame.CommandPool, nullptr);
    }
    _Frames.clear();
}

void VulkanHook_t::_InitializeForOverlay(VkDevice vulkanDevice, VkSwapchainKHR vulkanSwapChain)
{
    if (!_Initialized)
    {
        _VulkanDevice = vulkanDevice;

        if (!_FindApplicationHWND())
            return;

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        WindowsHook_t::Inst()->SetInitialWindowSize(_MainWindow);

        if (!_SetupVulkanRenderer())
        {
            _FreeVulkanRessources();
            return;
        }

        if (!_CreateRenderTargets(vulkanSwapChain))
        {
            _FreeVulkanRessources();
            return;
        }

        ImGui_ImplVulkan_LoadFunctions(_LoadVulkanFunction, this);

        ++_DescriptorsPools[0].UsedDescriptors;

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.PhysicalDevice = _VulkanPhysicalDevice;
        init_info.Device = _VulkanDevice;
        init_info.QueueFamily = _QueueFamilyIndex;
        init_info.Queue = _VulkanQueue;
        init_info.DescriptorPool = _DescriptorsPools[0].DescriptorPool;
        init_info.FontSampler = _VulkanImageSampler;
        init_info.FontCommandPool = _VulkanImageCommandPool;
        init_info.FontCommandBuffer = _VulkanImageCommandBuffer;
        init_info.FontDescriptorSet = _GetFreeDescriptorSet().DescriptorSet;
        init_info.DescriptorSetLayout = _VulkanDescriptorSetLayout;
        init_info.Subpass = 0;
        init_info.MinImageCount = _Frames.size();
        init_info.ImageCount = _Frames.size();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = (void(*)(VkResult))_CheckVkResult;

        ImGui_ImplVulkan_Init(&init_info);

        _Initialized = true;
    }

    if (_Frames.empty())
    {
        if (!_CreateRenderTargets(vulkanSwapChain))
            _ResetRenderState(OverlayHookState::Removing);
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
VulkanHook_t::VulkanFrame_t* VulkanHook_t::_PrepareForOverlay(uint32_t frameIndex)
{
    if (!_Initialized || _Frames.empty() || !ImGui_ImplVulkan_NewFrame() || !WindowsHook_t::Inst()->PrepareForOverlay(_MainWindow))
        return nullptr;

    auto& frame = _Frames[frameIndex];

    ImGui::NewFrame();

    OverlayProc();

    ImGui::Render();

    _vkWaitForFences(_VulkanDevice, 1, &frame.Fence, VK_TRUE, UINT64_MAX);

    _vkResetFences(_VulkanDevice, 1, &frame.Fence);

    {
        _vkResetCommandPool(_VulkanDevice, frame.CommandPool, 0);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        _vkBeginCommandBuffer(frame.CommandBuffer, &info);
    }
    {
        VkClearValue clearValue{};
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = _VulkanRenderPass;
        info.framebuffer = frame.Framebuffer;
        info.renderArea.extent.width = ImGui::GetIO().DisplaySize.x;
        info.renderArea.extent.height = ImGui::GetIO().DisplaySize.y;
        info.clearValueCount = 1;
        info.pClearValues = &clearValue;
        _vkCmdBeginRenderPass(frame.CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }
        
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.CommandBuffer, nullptr);
        
    _vkCmdEndRenderPass(frame.CommandBuffer);
    _vkEndCommandBuffer(frame.CommandBuffer);
        
    return &frame;
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    auto inst = VulkanHook_t::Inst();

    auto res = inst->_VkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);

    inst->_InitializeForOverlay(device, swapchain);

    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL VulkanHook_t::_MyVkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto inst = VulkanHook_t::Inst();
    auto phookPresentInfos = pPresentInfo;
    auto hookPresentInfos = *pPresentInfo;
    std::vector<VkSemaphore> semaphores(pPresentInfo->pWaitSemaphores, pPresentInfo->pWaitSemaphores + pPresentInfo->waitSemaphoreCount);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.commandBufferCount = 1;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
    {
        auto frame = inst->_PrepareForOverlay(pPresentInfo->pImageIndices[i]);
        if (frame != nullptr)
        {
            submitInfo.pSignalSemaphores = &frame->Semaphore;
            submitInfo.pCommandBuffers = &frame->CommandBuffer;
            submitInfo.pWaitDstStageMask = &waitStage;
            inst->_vkQueueSubmit(queue, 1, &submitInfo, frame->Fence);

            semaphores.emplace_back(frame->Semaphore);

            hookPresentInfos.pWaitSemaphores = semaphores.data();
            hookPresentInfos.waitSemaphoreCount = semaphores.size();
            phookPresentInfos = &hookPresentInfos;
        }
    }

    auto res = inst->_VkQueuePresentKHR(queue, phookPresentInfos);

    if (res != VkResult::VK_SUCCESS)
    {
        inst->_ResetRenderState(OverlayHookState::Reset);
    }

    return res;
}

VKAPI_ATTR void VKAPI_CALL VulkanHook_t::_MyVkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    auto inst = VulkanHook_t::Inst();
    if (inst->_VulkanDevice == device)
    {
        if (inst->_Initialized)
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext();
        }

        inst->_ResetRenderState(OverlayHookState::Removing);
    }

    inst->_VkDestroyDevice(device, pAllocator);
}

VulkanHook_t::VulkanHook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    _MainWindow(nullptr),
    _VulkanPhysicalDevice(nullptr),
    _VulkanInstance(nullptr),
    _VulkanDevice(nullptr),
    _VulkanQueue(nullptr),
    _QueueFamilyIndex(0),
    _VulkanDescriptorSetLayout(nullptr),
    _VulkanRenderPass(nullptr),
    _VulkanImageSampler(nullptr),
    _VulkanImageCommandPool(nullptr),
    _VulkanImageCommandBuffer(nullptr),
    _ImGuiFontAtlas(nullptr),
    _VkAcquireNextImageKHR(nullptr),
    _VkQueuePresentKHR(nullptr),
    _VkDestroyDevice(nullptr),
    _vkCreateInstance(nullptr),
    _vkDestroyInstance(nullptr),
    _vkGetInstanceProcAddr(nullptr),
    _vkEnumerateInstanceExtensionProperties(nullptr),
    _vkGetDeviceQueue(nullptr),
    _vkQueueSubmit(nullptr),
    _vkQueueWaitIdle(nullptr),
    _vkCreateRenderPass(nullptr),
    _vkCmdBeginRenderPass(nullptr),
    _vkCmdEndRenderPass(nullptr),
    _vkDestroyRenderPass(nullptr),
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
    _vkEnumerateDeviceExtensionProperties(nullptr),
    _vkEnumeratePhysicalDevices(nullptr),
    _vkGetPhysicalDeviceProperties(nullptr),
    _vkGetPhysicalDeviceQueueFamilyProperties(nullptr),
    _vkGetPhysicalDeviceMemoryProperties(nullptr),
    _vkGetSwapchainImagesKHR(nullptr)
{
}

VulkanHook_t::~VulkanHook_t()
{
    SPDLOG_INFO("VulkanHook_t Hook removed");

    if (_WindowsHooked)
        delete WindowsHook_t::Inst();

    _Instance = nullptr;
}

VulkanHook_t* VulkanHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new VulkanHook_t;

    return _Instance;
}

const std::string& VulkanHook_t::GetLibraryName() const
{
    return LibraryName;
}

void VulkanHook_t::LoadFunctions(
    decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR,
    decltype(::vkQueuePresentKHR)* vkQueuePresentKHR,
    decltype(::vkDestroyDevice)* vkDestroyDevice,
    std::function<void* (const char*)> vulkanFunctionLoader)
{
    _VkAcquireNextImageKHR = vkAcquireNextImageKHR;
    _VkQueuePresentKHR = vkQueuePresentKHR;
    _VkDestroyDevice = vkDestroyDevice;
    _VulkanFunctionLoader = std::move(vulkanFunctionLoader);
}

std::weak_ptr<uint64_t> VulkanHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    std::shared_ptr<uint64_t> image;
    VkResult result;
    VkImage vulkanImage = nullptr;
    VkImageView vulkanImageView = nullptr;
    VkDeviceMemory vulkanImageMemory = nullptr;
    VulkanDescriptorSet_t vulkanImageDescriptor;
    VkDeviceMemory uploadBufferMemory = nullptr;
    VkBuffer uploadBuffer = nullptr;

    if (!_CreateImageObjects())
        goto OnErrorCreateImage;
    
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
    
    size_t uploadSize = width * height * 4 * sizeof(char);
    
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
        result = _vkCreateImage(_VulkanDevice, &info, nullptr, &vulkanImage);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        VkMemoryRequirements req;
        _vkGetImageMemoryRequirements(_VulkanDevice, vulkanImage, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = _GetVulkanMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, req.memoryTypeBits);
        result = _vkAllocateMemory(_VulkanDevice, &alloc_info, nullptr, &vulkanImageMemory);
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
        result = _vkCreateImageView(_VulkanDevice, &info, nullptr, &vulkanImageView);
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
        result = _vkCreateBuffer(_VulkanDevice, &buffer_info, nullptr, &uploadBuffer);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        VkMemoryRequirements req;
        _vkGetBufferMemoryRequirements(_VulkanDevice, uploadBuffer, &req);
        
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = _GetVulkanMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
        result = _vkAllocateMemory(_VulkanDevice, &alloc_info, nullptr, &uploadBufferMemory);
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
        _vkCmdPipelineBarrier(_VulkanImageCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, copyBarrier);
    
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

        result = _vkQueueSubmit(_VulkanQueue, 1, &endInfo, nullptr);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;


        result = _vkQueueWaitIdle(_VulkanQueue);
        _CheckVkResult(result);
        if (result != VkResult::VK_SUCCESS)
            goto OnErrorCreateImage;

        _vkDestroyImage(_VulkanDevice, vulkanImage, nullptr);
        _vkDestroyBuffer(_VulkanDevice, uploadBuffer, nullptr);
        _vkFreeMemory(_VulkanDevice, uploadBufferMemory, nullptr);
        _vkDestroyImageView(_VulkanDevice, vulkanImageView, nullptr);
    }

    struct VulkanImage_t
    {
        ImTextureID ImageId;
        VkDeviceMemory VulkanImageMemory = nullptr;
        uint32_t ImagePoolId;
    };

    VulkanImage_t* vulkanRendererImage = new VulkanImage_t;
    vulkanRendererImage->ImageId = (ImTextureID)vulkanImageDescriptor.DescriptorSet;
    vulkanRendererImage->VulkanImageMemory = vulkanImageMemory;
    vulkanRendererImage->ImagePoolId = vulkanImageDescriptor.DescriptorPoolId;

    image = std::shared_ptr<uint64_t>((uint64_t*)vulkanRendererImage, [this](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            auto vulkanImage = reinterpret_cast<VulkanImage_t*>(handle);
            
            _vkFreeMemory(_VulkanDevice, vulkanImage->VulkanImageMemory, nullptr);
            _ReleaseDescriptor(vulkanImage->ImagePoolId, (VkDescriptorSet)vulkanImage->ImageId);

            delete vulkanImage;
        }
    });

    _ImageResources.emplace(image);

    return image;

OnErrorCreateImage:
    if (vulkanImageDescriptor.DescriptorSet != nullptr) _ReleaseDescriptor(vulkanImageDescriptor.DescriptorPoolId, vulkanImageDescriptor.DescriptorSet);
    if (uploadBuffer                        != nullptr) _vkDestroyBuffer   (_VulkanDevice, uploadBuffer      , nullptr);
    if (uploadBufferMemory                  != nullptr) _vkFreeMemory      (_VulkanDevice, uploadBufferMemory, nullptr);
    if (vulkanImageView                     != nullptr) _vkDestroyImageView(_VulkanDevice, vulkanImageView   , nullptr);
    if (vulkanImageMemory                   != nullptr) _vkFreeMemory      (_VulkanDevice, vulkanImageMemory , nullptr);
    if (vulkanImage                         != nullptr) _vkDestroyImage    (_VulkanDevice, vulkanImage       , nullptr);

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