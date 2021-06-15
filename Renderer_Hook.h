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

#include "Base_Hook.h"
#include "image.h"
#include <memory>
#include <set>
#include <functional>

#include "utils.h"

#if   defined(UTILS_OS_WINDOWS)

#include <Windows.h>

#elif defined(UTILS_OS_LINUX)
#endif

class Renderer_Hook : public Base_Hook
{
public:
    Renderer_Hook():
        overlay_proc(&default_overlay_proc),
        overlay_hook_ready(&default_overlay_hook_ready)
    {}

    std::string library_name;

    static void default_overlay_proc() {}
    static void default_overlay_hook_ready(bool) {}
    std::function<void()> overlay_proc;
    std::function<void(bool)> overlay_hook_ready;

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback) = 0;
    virtual bool is_started() = 0;
    // Returns a Handle to the renderer image ressource or nullptr if it failed to create the resource, the handle can be used in ImGui's Image calls, image_buffer must be RGBA ordered
    virtual std::weak_ptr<uint64_t> CreateImageResource(std::shared_ptr<Image> source) = 0;
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource) = 0;
};
