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

#include <spdlog/spdlog.h>
#include <mini_detour.h>
#include <vector>

class Base_Hook
{
protected:
    std::vector<mini_detour::hook> _hooked_funcs;

    Base_Hook(const Base_Hook&) = delete;
    Base_Hook(Base_Hook&&) = delete;
    Base_Hook& operator =(const Base_Hook&) = delete;
    Base_Hook& operator =(Base_Hook&&) = delete;

public:
    Base_Hook();
    virtual ~Base_Hook();

    void BeginHook();
    void EndHook();
    void UnhookAll();

    virtual const char* get_lib_name() const;
    void HookFunc(std::pair<void**, void*> hook);

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
