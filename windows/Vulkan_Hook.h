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

#include "../Renderer_Hook.h"

#include <vulkan/vulkan.h>

class Vulkan_Hook : public Renderer_Hook
{
public:
    static constexpr const char *DLL_NAME = "vulkan-1.dll";

private:
    static Vulkan_Hook* _inst;

    // Variables
    bool hooked;
    bool windows_hooked;
    bool initialized;

    // Functions
    Vulkan_Hook();

    void resetRenderState();
    void prepareForOverlay();

    // Hook to render functions

public:
    virtual ~Vulkan_Hook();

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback);
    virtual bool is_started();
    static Vulkan_Hook* Inst();
    virtual const char* get_lib_name() const;
    void loadFunctions();

    virtual std::weak_ptr<uint64_t> CreateImageResource(std::shared_ptr<Image> source);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
