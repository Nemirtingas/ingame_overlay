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
#include <library/library.h>

#include "objc_wrappers.h"

constexpr decltype(NSView_Hook::DLL_NAME) NSView_Hook::DLL_NAME;

NSView_Hook* NSView_Hook::_inst = nullptr;

bool NSView_Hook::start_hook(std::function<bool(bool)>& _key_combination_callback)
{
    if (!hooked)
    {
        void* hAppKit = Library::get_module_handle(DLL_NAME);
        Library libAppKit;
        library_name = Library::get_module_path(hAppKit);
        if (!libAppKit.load_library(library_name, false))
        {
            SPDLOG_WARN("Failed to hook NSView: Cannot load {}", library_name);
            return false;
        }

        SPDLOG_INFO("Hooked NSView");

        nsview_hook = new ObjCHookWrapper();
        
        key_combination_callback = std::move(_key_combination_callback);
        hooked = true;
    }
    return true;
}

void NSView_Hook::resetRenderState()
{
    if (initialized)
    {
        nsview_hook->UnhookMainWindow();
        initialized = false;
        ImGui_ImplOSX_Shutdown();
    }
}

bool NSView_Hook::prepareForOverlay()
{
    if (!hooked)
        return false;

    if(!initialized)
    {
        if (!nsview_hook->HookMainWindow())
        {
            SPDLOG_WARN("Failed to start NSView hook.");
            return false;
        }
        nsview_hook->SetEventHandler(&handleNSEvent);

        ImGui_ImplOSX_Init();
    }
    
    ImGui_ImplOSX_NewFrame(nsview_hook->GetNSView().NSView());
    return true;
}

bool NSView_Hook::handleNSEvent(cppNSEvent* cppevent, cppNSView* cppview)
{
    NSView_Hook* inst = Inst();
    bool ignore_event = inst->key_combination_callback(false);
    
    switch(cppevent->Type())
    {
        case cppNSEventTypeKeyDown:
        {
            auto* eventKey = cppevent->Key();
            
            if(!eventKey->IsARepeat() &&
               eventKey->KeyCode() == cppkVK_Tab &&
               eventKey->Modifier() & cppNSEventModifierFlagShift)
            {
                ignore_event = true;
                inst->key_combination_callback(true);
            }
        }
        break;
            
        case cppNSEventTypeLeftMouseDragged:
        case cppNSEventTypeLeftMouseDown:
        case cppNSEventTypeMouseMoved:
        {
            auto* eventMouse = cppevent->Mouse();
            auto bounds = cppview->Bounds();
            // 3 pixels outside the window and 2 pixels inside.
            if((eventMouse->X() >= -4.0f && eventMouse->X() <= 3.0f) ||
               (eventMouse->X() >= (bounds.size.width - 2) && eventMouse->X() <= (bounds.size.width + 3.0f)) ||
               (eventMouse->Y() >= -4.0f && eventMouse->Y() <= 3.0f) ||
               (eventMouse->Y() >= (bounds.size.height - 2) && eventMouse->Y() <= (bounds.size.height + 3.0f)))
            {// Allow window resize
                ignore_event = false;
            }
        }
        break;
    }
    
    ImGui_ImplOSX_HandleEvent(cppevent->NSEvent(), cppview->NSView());
    return ignore_event;
}

/////////////////////////////////////////////////////////////////////////////////////

NSView_Hook::NSView_Hook() :
    initialized(false),
    hooked(false),
    nsview_hook(nullptr)
{
}

NSView_Hook::~NSView_Hook()
{
    SPDLOG_INFO("NSView Hook removed");

    resetRenderState();

    _inst = nullptr;
}

NSView_Hook* NSView_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new NSView_Hook;

    return _inst;
}

const char* NSView_Hook::get_lib_name() const
{
    return library_name.c_str();
}
