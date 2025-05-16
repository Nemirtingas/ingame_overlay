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

#include <vector>
#include <array>
#include <utility>
#include <mini_detour/mini_detour.h>

class BaseHook_t
{
protected:
    std::vector<MiniDetour::Hook_t> _HookedFunctions;

    BaseHook_t(const BaseHook_t&) = delete;
    BaseHook_t(BaseHook_t&&) = delete;
    BaseHook_t& operator =(const BaseHook_t&) = delete;
    BaseHook_t& operator =(BaseHook_t&&) = delete;

public:
    BaseHook_t();
    virtual ~BaseHook_t();

    void BeginHook();
    void EndHook();
    void UnhookAll();

    bool HookFunc(std::pair<void**, void*> hook);

    template<typename T>
    void HookFuncs(std::pair<T*, T> funcs)
    {
        HookFunc(funcs);
    }

    template<typename T, typename ...Args>
    void HookFuncs(std::pair<T*, T> funcs, Args... args)
    {
        HookFunc(funcs);
        HookFuncs(args...);
    }
};
