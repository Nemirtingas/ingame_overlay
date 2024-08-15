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

#include <vulkan/vulkan.h>

#include <vector>

#include <string.h>

constexpr int SelectDeviceTypeStart = 5; // countof(deviceTypePriorities)

static bool IsVulkanExtensionAvailable(const std::vector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;

    return false;
}

static int32_t GetVulkanFirstGraphicsQueue(std::vector<VkQueueFamilyProperties> const& queues)
{
    for (uint32_t i = 0; i < queues.size(); i++)
    {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return i;
    }

    return -1;
}

static bool SelectVulkanPhysicalDeviceType(VkPhysicalDeviceType deviceType, int& deviceTypeSelected)
{
    constexpr VkPhysicalDeviceType deviceTypePriorities[] = {
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
        VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
        VK_PHYSICAL_DEVICE_TYPE_CPU,
        VK_PHYSICAL_DEVICE_TYPE_OTHER,
    };

    for (int i = 0; i < deviceTypeSelected; ++i)
    {
        if (deviceTypePriorities[i] == deviceType)
        {
            deviceTypeSelected = i;
            return true;
        }
    }

    return false;
}

static int32_t GetVulkanPhysicalDeviceFirstGraphicsQueue(decltype(::vkGetPhysicalDeviceQueueFamilyProperties)* _vkGetPhysicalDeviceQueueFamilyProperties, VkPhysicalDevice physicalDevice)
{
    uint32_t count;
    std::vector<VkQueueFamilyProperties> queues;
    _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    queues.resize(count);
    _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queues.data());

    return GetVulkanFirstGraphicsQueue(queues);
}

static bool VulkanPhysicalDeviceHasExtension(decltype(::vkEnumerateDeviceExtensionProperties)* _vkEnumerateDeviceExtensionProperties, VkPhysicalDevice vkPhysicalDevice, const char* extensionName)
{
    uint32_t count;
    std::vector<VkExtensionProperties> extensionProperties;

    _vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &count, nullptr);
    extensionProperties.resize(count);
    _vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &count, extensionProperties.data());

    return IsVulkanExtensionAvailable(extensionProperties, extensionName);
}