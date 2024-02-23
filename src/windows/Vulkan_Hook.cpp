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

#include "Vulkan_Hook.h"
#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

static bool _IsExtensionAvailable(const std::vector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;

    return false;
}

static void _CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    SPDLOG_ERROR("[vulkan] Error: VkResult = {}", (int)err);
}

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(PVOID&)_##NAME, &Vulkan_Hook::My##NAME))) { \
    SPDLOG_ERROR("Failed to hook {}", #NAME);\
    return false;\
} } while(0)

Vulkan_Hook* Vulkan_Hook::_inst = nullptr;

bool Vulkan_Hook::StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_vkAcquireNextImageKHR == nullptr || _vkQueuePresentKHR == nullptr || _VulkanFunctionLoader == nullptr)
        {
            SPDLOG_WARN("Failed to hook Vulkan: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION(vkAcquireNextImageKHR);
        TRY_HOOK_FUNCTION(vkQueuePresentKHR);
        EndHook();

        SPDLOG_INFO("Hooked Vulkan");
        _Hooked = true;
        _ImGuiFontAtlas = imgui_font_atlas;
    }
    return true;
}

void Vulkan_Hook::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideAppInputs(hide);
    }
}

void Vulkan_Hook::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        Windows_Hook::Inst()->HideOverlayInputs(hide);
    }
}

bool Vulkan_Hook::IsStarted()
{
    return _Hooked;
}

void Vulkan_Hook::_ResetRenderState()
{
    // TODO: Hook vkDestroySwapchainKHR and probably vkDestroyDevice.
    if (_Initialized)
    {
        OverlayHookReady(ingame_overlay::OverlayHookState::Removing);

        ImGui_ImplVulkan_Shutdown();
        Windows_Hook::Inst()->ResetRenderState();
        ImGui::DestroyContext();

        _FreeVulkanRessources();
        //_ImageResources.clear();

        _Initialized = false;
    }
}

PFN_vkVoidFunction Vulkan_Hook::_LoadVulkanFunction(const char* functionName, void* userData)
{
    return reinterpret_cast<Vulkan_Hook*>(userData)->_LoadVulkanFunction(functionName);
}

PFN_vkVoidFunction Vulkan_Hook::_LoadVulkanFunction(const char* functionName)
{
    return _vkGetInstanceProcAddr(_VulkanInstance, functionName);
}

bool Vulkan_Hook::_FindApplicationHWND()
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

void Vulkan_Hook::_FreeVulkanRessources()
{
    if (_VulkanInstance == nullptr)
        return;

    if (_VulkanDevice != nullptr)
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

        if (_VulkanDescriptorPool != nullptr)
        {
            _vkDestroyDescriptorPool(_VulkanDevice, _VulkanDescriptorPool, nullptr);
            _VulkanDescriptorPool = nullptr;
        }

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

bool Vulkan_Hook::_CreateVulkanInstance()
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
        LOAD_VULKAN_FUNCTION(vkGetDeviceQueue);
        LOAD_VULKAN_FUNCTION(vkCreateRenderPass);
        LOAD_VULKAN_FUNCTION(vkDestroyRenderPass);
        LOAD_VULKAN_FUNCTION(vkCmdBeginRenderPass);
        LOAD_VULKAN_FUNCTION(vkCmdEndRenderPass);
        LOAD_VULKAN_FUNCTION(vkCreateSemaphore);
        LOAD_VULKAN_FUNCTION(vkDestroySemaphore);
        LOAD_VULKAN_FUNCTION(vkCreateCommandPool);
        LOAD_VULKAN_FUNCTION(vkResetCommandPool);
        LOAD_VULKAN_FUNCTION(vkDestroyCommandPool);
        LOAD_VULKAN_FUNCTION(vkCreateImageView);
        LOAD_VULKAN_FUNCTION(vkDestroyImageView);
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
        LOAD_VULKAN_FUNCTION(vkEnumerateDeviceExtensionProperties);
        LOAD_VULKAN_FUNCTION(vkEnumeratePhysicalDevices);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceProperties);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
        LOAD_VULKAN_FUNCTION(vkGetSwapchainImagesKHR);
#undef LOAD_VULKAN_FUNCTION
    }

    return result;
}

int32_t Vulkan_Hook::_GetPhysicalDeviceFirstGraphicsQueue(VkPhysicalDevice physicalDevice)
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

bool Vulkan_Hook::_GetPhysicalDevice()
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
    
            std::vector<VkQueueFamilyProperties> queues;
            _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &count, nullptr);
            queues.resize(count);
            _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &count, queues.data());
            for (uint32_t i = 0; i < count; i++)
            {
                if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    queueFamilyIndex = i;
                    break;
                }
            }

            _vkGetDeviceQueue(_VulkanDevice, queueFamilyIndex, 0, &vulkanQueue);
            
            _QueueFamilyIndex = queueFamilyIndex;
            _VulkanPhysicalDevice = physicalDevices[deviceIndex];
            _VulkanQueue = vulkanQueue;
            return true;
        }
    }

    return false;
}

bool Vulkan_Hook::_CreateDescriptorPool()
{
    VkDescriptorPool vulkanDescriptorPool;

    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCreateInfo.maxSets = 1;
    descriptorPoolCreateInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    if (_vkCreateDescriptorPool(_VulkanDevice, &descriptorPoolCreateInfo, nullptr, &vulkanDescriptorPool) != VkResult::VK_SUCCESS || vulkanDescriptorPool == nullptr)
        return false;

    _VulkanDescriptorPool = vulkanDescriptorPool;
    return true;
}

bool Vulkan_Hook::_CreateRenderPass()
{
    VkRenderPass vulkanRenderPass;
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

bool Vulkan_Hook::_CreateRenderTargets(VkImage* backBuffers, uint32_t backBufferCount)
{
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

bool Vulkan_Hook::_SetupVulkanRenderer(VkSwapchainKHR swapChain)
{
    if (!_CreateVulkanInstance())
        return false;

    if (!_GetPhysicalDevice())
        return false;

    if (!_CreateDescriptorPool())
        return false;

    if (!_CreateRenderPass())
        return false;

    uint32_t backBufferCount;
    std::vector<VkImage> backBuffers;
    _vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &backBufferCount, nullptr);
    backBuffers.resize(backBufferCount);
    _vkGetSwapchainImagesKHR(_VulkanDevice, swapChain, &backBufferCount, backBuffers.data());

    if (!_CreateRenderTargets(backBuffers.data(), backBufferCount))
        return false;

    return true;
}

void Vulkan_Hook::_InitializeForOverlay(VkDevice vulkanDevice, VkSwapchainKHR vulkanSwapChain, uint32_t frameIndex)
{
    if (!_Initialized)
    {
        _VulkanDevice = vulkanDevice;

        if (!_FindApplicationHWND())
            return;

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));
        Windows_Hook::Inst()->SetInitialWindowSize(_MainWindow);

        if (!_SetupVulkanRenderer(vulkanSwapChain))
        {
            _FreeVulkanRessources();
            return;
        }

        ImGui_ImplVulkan_LoadFunctions(_LoadVulkanFunction, this);

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.PhysicalDevice = _VulkanPhysicalDevice;
        init_info.Device = _VulkanDevice;
        init_info.QueueFamily = _QueueFamilyIndex;
        init_info.Queue = _VulkanQueue;
        init_info.DescriptorPool = _VulkanDescriptorPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = _Frames.size();
        init_info.ImageCount = _Frames.size();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = _CheckVkResult;

        ImGui_ImplVulkan_Init(&init_info);

        _Initialized = true;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
Vulkan_Hook::VulkanFrame_t* Vulkan_Hook::_PrepareForOverlay(uint32_t frameIndex)
{
    if (!_Initialized || !ImGui_ImplVulkan_NewFrame() || !Windows_Hook::Inst()->PrepareForOverlay(_MainWindow))
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

VKAPI_ATTR VkResult VKAPI_CALL Vulkan_Hook::MyvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    auto inst = Vulkan_Hook::Inst();

    uint32_t frameIndex;
    auto res = inst->_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, &frameIndex);

    inst->_InitializeForOverlay(device, swapchain, frameIndex);
    if (pImageIndex != nullptr)
        *pImageIndex = frameIndex;

    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL Vulkan_Hook::MyvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto inst = Vulkan_Hook::Inst();
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

    auto res = inst->_vkQueuePresentKHR(queue, phookPresentInfos);

    return res;
}

Vulkan_Hook::Vulkan_Hook():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    _MainWindow(nullptr),
    _VulkanPhysicalDevice(nullptr),
    _VulkanInstance(nullptr),
    _VulkanDevice(nullptr),
    _VulkanQueue(nullptr),
    _QueueFamilyIndex(0),
    _VulkanDescriptorPool(nullptr),
    _VulkanRenderPass(nullptr),
    _ImGuiFontAtlas(nullptr),
    _vkAcquireNextImageKHR(nullptr),
    _vkQueuePresentKHR(nullptr),
    _vkCreateInstance(nullptr),
    _vkDestroyInstance(nullptr),
    _vkGetInstanceProcAddr(nullptr),
    _vkEnumerateInstanceExtensionProperties(nullptr),
    _vkGetDeviceQueue(nullptr),
    _vkQueueSubmit(nullptr),
    _vkCreateRenderPass(nullptr),
    _vkCmdBeginRenderPass(nullptr),
    _vkCmdEndRenderPass(nullptr),
    _vkDestroyRenderPass(nullptr),
    _vkCreateSemaphore(nullptr),
    _vkDestroySemaphore(nullptr),
    _vkCreateCommandPool(nullptr),
    _vkResetCommandPool(nullptr),
    _vkDestroyCommandPool(nullptr),
    _vkCreateImageView(nullptr),
    _vkDestroyImageView(nullptr),
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
    _vkEnumerateDeviceExtensionProperties(nullptr),
    _vkEnumeratePhysicalDevices(nullptr),
    _vkGetPhysicalDeviceProperties(nullptr),
    _vkGetPhysicalDeviceQueueFamilyProperties(nullptr),
    _vkGetSwapchainImagesKHR(nullptr)
{
}

Vulkan_Hook::~Vulkan_Hook()
{
    SPDLOG_INFO("Vulkan_Hook Hook removed");

    if (_WindowsHooked)
        delete Windows_Hook::Inst();

    if (_Initialized)
    {
        //ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
    }

    _inst = nullptr;
}

Vulkan_Hook* Vulkan_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new Vulkan_Hook;

    return _inst;
}

const std::string& Vulkan_Hook::GetLibraryName() const
{
    return LibraryName;
}

void Vulkan_Hook::LoadFunctions(
    decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR,
    decltype(::vkQueuePresentKHR)* vkQueuePresentKHR,
    std::function<void* (const char*)> vulkanFunctionLoader)
{
    _vkAcquireNextImageKHR = vkAcquireNextImageKHR;
    _vkQueuePresentKHR = vkQueuePresentKHR;
    _VulkanFunctionLoader = std::move(vulkanFunctionLoader);
}

std::weak_ptr<uint64_t> Vulkan_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>(nullptr);
}

void Vulkan_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{

}