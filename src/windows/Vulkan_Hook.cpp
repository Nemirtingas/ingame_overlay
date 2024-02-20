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

Vulkan_Hook* Vulkan_Hook::_inst = nullptr;

bool Vulkan_Hook::StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    SPDLOG_WARN("Vulkan overlay is not yet supported.");
    
    if (!_Hooked)
    {
        if (_vkCmdEndRenderPass == nullptr || _VulkanFunctionLoader == nullptr)
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
            std::make_pair<void**, void*>(&(PVOID&)_vkCmdEndRenderPass, &Vulkan_Hook::MyvkCmdEndRenderPass)
        );
        EndHook();
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
    if (_Initialized)
    {
        OverlayHookReady(ingame_overlay::OverlayHookState::Removing);

        ImGui_ImplVulkan_Shutdown();
        Windows_Hook::Inst()->ResetRenderState();
        ImGui::DestroyContext();

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
    if (_VulkanDescriptorPool != nullptr)
    {
        _vkDestroyDescriptorPool(_VulkanDevice, _VulkanDescriptorPool, nullptr);
        _VulkanDescriptorPool = nullptr;
    }

    if (_VulkanDevice != nullptr)
    {
        _vkDestroyDevice(_VulkanDevice, nullptr);
        _VulkanDevice = nullptr;
        _VulkanQueue = nullptr;
    }

    if (_VulkanInstance != nullptr)
    {
        _vkDestroyInstance(_VulkanInstance, nullptr);
        _VulkanInstance = nullptr;
        _VulkanPhysicalDevice = nullptr;
    }
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
        LOAD_VULKAN_FUNCTION(vkCreateDevice);
        LOAD_VULKAN_FUNCTION(vkDestroyDevice);
        LOAD_VULKAN_FUNCTION(vkGetDeviceQueue);
        LOAD_VULKAN_FUNCTION(vkCreateDescriptorPool);
        LOAD_VULKAN_FUNCTION(vkDestroyDescriptorPool);
        LOAD_VULKAN_FUNCTION(vkEnumerateDeviceExtensionProperties);
        LOAD_VULKAN_FUNCTION(vkEnumeratePhysicalDevices);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceProperties);
        LOAD_VULKAN_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
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

bool Vulkan_Hook::_GetPhysicalDeviceAndCreateLogicalDevice()
{
    std::vector<VkPhysicalDevice> physicalDevices;
    std::vector<uint32_t> preferedPhysicalDevicesIndex;
    std::vector<VkExtensionProperties> extensionProperties;
    std::vector<VkQueueFamilyProperties> queuesFamilyProperties;
    VkPhysicalDeviceProperties physicalDevicesProperties;
    VkDevice vulkanDevice;
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

            deviceCreateInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.enabledExtensionCount = 1;
            const char* str = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            deviceCreateInfo.ppEnabledExtensionNames = &str;

            const float queue_priority[] = { 1.0f };
            VkDeviceQueueCreateInfo queue_info[1] = {};
            queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info[0].queueFamilyIndex = queueFamilyIndex;
            queue_info[0].queueCount = 1;
            queue_info[0].pQueuePriorities = queue_priority;

            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
            deviceCreateInfo.pQueueCreateInfos = queue_info;

            if (_vkCreateDevice(physicalDevices[deviceIndex], &deviceCreateInfo, nullptr, &vulkanDevice) == VkResult::VK_SUCCESS && vulkanDevice != nullptr)
            {
                _vkGetDeviceQueue(vulkanDevice, queueFamilyIndex, 0, &vulkanQueue);
            
                _QueueFamilyIndex = queueFamilyIndex;
                _VulkanPhysicalDevice = physicalDevices[deviceIndex];
                _VulkanDevice = vulkanDevice;
                _VulkanQueue = vulkanQueue;
                return true;
            }
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

bool Vulkan_Hook::_SetupVulkanRenderer()
{
    if (!_CreateVulkanInstance())
        return false;

    if (!_GetPhysicalDeviceAndCreateLogicalDevice())
        return false;

    if (!_CreateDescriptorPool())
        return false;

    return true;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void Vulkan_Hook::_PrepareForOverlay(VkCommandBuffer commandBuffer)
{
    if (!_Initialized)
    {
        if (!_FindApplicationHWND())
            return;

        if (!_SetupVulkanRenderer())
        {
            _FreeVulkanRessources();
            return;
        }

        ImGui_ImplVulkan_LoadFunctions(_LoadVulkanFunction, this);

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = _VulkanInstance;
        init_info.PhysicalDevice = _VulkanPhysicalDevice;
        init_info.Device = _VulkanDevice;
        init_info.QueueFamily = _QueueFamilyIndex;
        init_info.Queue = _VulkanQueue;
        init_info.DescriptorPool = _VulkanDescriptorPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = _CheckVkResult;

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplVulkan_Init(&init_info);

        Windows_Hook::Inst()->SetInitialWindowSize(_MainWindow);

        _Initialized = true;
    }

    if (ImGui_ImplVulkan_NewFrame() && Windows_Hook::Inst()->PrepareForOverlay(_MainWindow))
    {
        ImGui::NewFrame();

        OverlayProc();

        ImGui::Render();

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
}

VKAPI_ATTR void VKAPI_CALL Vulkan_Hook::MyvkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    auto inst = Vulkan_Hook::Inst();
    inst->_PrepareForOverlay(commandBuffer);
    inst->_vkCmdEndRenderPass(commandBuffer);
}

Vulkan_Hook::Vulkan_Hook():
    _Hooked(false),
    _WindowsHooked(false),
    _Initialized(false),
    _VulkanPhysicalDevice(nullptr),
    _VulkanInstance(nullptr),
    _VulkanDevice(nullptr),
    _QueueFamilyIndex(0),
    _VulkanQueue(nullptr),
    _VulkanDescriptorPool(nullptr),
    _ImGuiFontAtlas(nullptr),
    _vkCreateInstance(nullptr),
    _vkDestroyInstance(nullptr),
    _vkGetInstanceProcAddr(nullptr),
    _vkEnumerateInstanceExtensionProperties(nullptr),
    _vkCreateDevice(nullptr),
    _vkGetDeviceQueue(nullptr),
    _vkDestroyDevice(nullptr),
    _vkCreateDescriptorPool(nullptr),
    _vkDestroyDescriptorPool(nullptr),
    _vkCmdEndRenderPass(nullptr),
    _vkEnumerateDeviceExtensionProperties(nullptr),
    _vkEnumeratePhysicalDevices(nullptr),
    _vkGetPhysicalDeviceProperties(nullptr),
    _vkGetPhysicalDeviceQueueFamilyProperties(nullptr)
{
}

Vulkan_Hook::~Vulkan_Hook()
{
    SPDLOG_INFO("Vulkan_Hook Hook removed");

    if (_WindowsHooked)
        delete Windows_Hook::Inst();

    if (_Initialized)
    {
    }

    _inst = nullptr;
}

Vulkan_Hook* Vulkan_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new Vulkan_Hook;

    return _inst;
}

std::string Vulkan_Hook::GetLibraryName() const
{
    return LibraryName;
}

void Vulkan_Hook::LoadFunctions(decltype(::vkCmdEndRenderPass)* vkCmdEndRenderPass, std::function<void* (const char*)> vulkanFunctionLoader)
{
    _vkCmdEndRenderPass = vkCmdEndRenderPass;
    _VulkanFunctionLoader = std::move(vulkanFunctionLoader);
}

std::weak_ptr<uint64_t> Vulkan_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>(nullptr);
}

void Vulkan_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{

}