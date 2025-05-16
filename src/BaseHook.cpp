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

#include "BaseHook.h"

#include <algorithm>
#include <mini_detour/mini_detour.h>

BaseHook_t::BaseHook_t()
{}

BaseHook_t::~BaseHook_t()
{
    UnhookAll();
}

void BaseHook_t::BeginHook()
{
    //mini_detour::transaction_begin();
}

void BaseHook_t::EndHook()
{
    //mini_detour::transaction_commit();
}

bool BaseHook_t::HookFunc(std::pair<void**, void*> hook)
{
    MiniDetour::Hook_t md_hook;
    void* res = md_hook.HookFunction(*hook.first, hook.second);
    if (res == nullptr)
        return false;

    _HookedFunctions.emplace_back(std::move(md_hook));
    *hook.first = res;
    return true;
}

void BaseHook_t::UnhookAll()
{
    _HookedFunctions.clear();
}
