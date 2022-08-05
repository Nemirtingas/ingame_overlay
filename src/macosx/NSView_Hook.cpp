/*
 * Copyright (C) 2019-2020 Nemirtingas
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

#include "NSView_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_osx.h>
#include <System/Library.h>

#include "objc_wrappers.h"

constexpr decltype(NSView_Hook::DLL_NAME) NSView_Hook::DLL_NAME;

NSView_Hook* NSView_Hook::_inst = nullptr;

bool NSView_Hook::StartHook(std::function<bool(bool)>& _key_combination_callback)
{
    if (!_Hooked)
    {
        void* hAppKit = System::Library::GetLibraryHandle(DLL_NAME);
        if (hAppKit == nullptr)
        {
            SPDLOG_WARN("Failed to hook NSView: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library::Library libAppKit;
        LibraryName = System::Library::GetLibraryPath(hAppKit);
        if (!libAppKit.OpenLibrary(LibraryName, false))
        {
            SPDLOG_WARN("Failed to hook NSView: Cannot load {}", LibraryName);
            return false;
        }

        _NSViewHook = new ObjCHookWrapper();
        
        SPDLOG_INFO("Hooked NSView");
        KeyCombinationCallback = std::move(_key_combination_callback);
        _Hooked = true;
    }
    return true;
}

void NSView_Hook::ResetRenderState()
{
    if (_Initialized)
    {
        _NSViewHook->UnhookMainWindow();
        _Initialized = false;
        ImGui_ImplOSX_Shutdown();
    }
}

bool NSView_Hook::PrepareForOverlay()
{
    if (!_Hooked)
        return false;

    if(!_Initialized)
    {
        if (!_NSViewHook->HookMainWindow())
        {
            SPDLOG_WARN("Failed to start NSView hook.");
            return false;
        }

        double width, height;

        get_window_size_from_sharedApplication(&width, &height);

        ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);

        ImGui_ImplOSX_Init();
    }
    
    ImGui_ImplOSX_NewFrame(_NSViewHook->GetNSView());
    return true;
}

bool NSView_Hook::IgnoreInputs()
{
    if(KeyCombinationCallback == nullptr)
        return false;

    return KeyCombinationCallback(false);
}

void NSView_Hook::HandleNSEvent(void* _NSEvent, void* _NSView)
{
    ImGui_ImplOSX_HandleEvent(_NSEvent, _NSView);
}

/////////////////////////////////////////////////////////////////////////////////////

NSView_Hook::NSView_Hook() :
    _Initialized(false),
    _Hooked(false),
    _NSViewHook(nullptr)
{
}

NSView_Hook::~NSView_Hook()
{
    SPDLOG_INFO("NSView Hook removed");

    ResetRenderState();

    _inst = nullptr;
}

NSView_Hook* NSView_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new NSView_Hook;

    return _inst;
}

std::string NSView_Hook::GetLibraryName() const
{
    return LibraryName;
}

bool NSView_Hook_IgnoreInputs()
{
	return NSView_Hook::Inst()->IgnoreInputs();
}

void NSView_Hook_HandleNSEvent(void* _NSEvent, void* _NSView)
{
	NSView_Hook::Inst()->HandleNSEvent(_NSEvent, _NSView);
}

bool NSView_Hook_KeyCallback(bool v)
{
	auto* inst = NSView_Hook::Inst();
	if(inst->KeyCombinationCallback == nullptr)
		return false;
	
	return inst->KeyCombinationCallback(v);
}