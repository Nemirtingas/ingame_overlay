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

Vulkan_Hook* Vulkan_Hook::_inst = nullptr;

bool Vulkan_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    SPDLOG_WARN("Vulkan overlay is not yet supported.");
    return false;
}

bool Vulkan_Hook::is_started()
{
    return hooked;
}

void Vulkan_Hook::resetRenderState()
{
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void Vulkan_Hook::prepareForOverlay()
{
}

Vulkan_Hook::Vulkan_Hook():
    hooked(false),
    windows_hooked(false),
    initialized(false)
{
}

Vulkan_Hook::~Vulkan_Hook()
{
    SPDLOG_INFO("Vulkan_Hook Hook removed");

    if (windows_hooked)
        delete Windows_Hook::Inst();

    if (initialized)
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

const char* Vulkan_Hook::get_lib_name() const
{
    return DLL_NAME;
}

void Vulkan_Hook::loadFunctions()
{
}

std::weak_ptr<uint64_t> Vulkan_Hook::CreateImageResource(std::shared_ptr<Image> source)
{
    return std::shared_ptr<uint64_t>(nullptr);
}

void Vulkan_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{

}