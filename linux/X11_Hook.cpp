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

#include "X11_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_x11.h>
#include <library/library.h>

extern int ImGui_ImplX11_EventHandler(XEvent &event);

constexpr decltype(X11_Hook::DLL_NAME) X11_Hook::DLL_NAME;

X11_Hook* X11_Hook::_inst = nullptr;

bool X11_Hook::start_hook(std::function<bool(bool)>& _key_combination_callback)
{
    if (!hooked)
    {
        void* hX11 = Library::get_module_handle(DLL_NAME);
        Library libX11;
        library_name = Library::get_module_path(hX11);
        
        if (!libX11.load_library(library_name, false))
        {
            SPDLOG_WARN("Failed to hook X11: Cannot load {}", library_name);
            return false;
        }

        XEventsQueued = (decltype(XEventsQueued))dlsym(library, "XEventsQueued");
        XPending = (decltype(XPending))dlsym(library, "XPending");

        if (XPending == nullptr || XEventsQueued == nullptr)
        {
            SPDLOG_WARN("Failed to hook X11: Cannot load functions.({}, {})", DLL_NAME, (void*)XEventsQueued, (void*)XPending);
            return false;
        }
        SPDLOG_INFO("Hooked X11");

        key_combination_callback = std::move(_key_combination_callback);
        hooked = true;

        UnhookAll();
        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(void*&)XEventsQueued, (void*)&X11_Hook::MyXEventsQueued),
            std::make_pair<void**, void*>(&(void*&)XPending, (void*)&X11_Hook::MyXPending)
        );
        EndHook();
    }
    return true;
}

void X11_Hook::resetRenderState()
{
    if (initialized)
    {
        game_wnd = 0;
        initialized = false;
        ImGui_ImplX11_Shutdown();
    }
}

bool X11_Hook::prepareForOverlay(Display *display, Window wnd)
{
    if(!hooked)
        return false;

    if (game_wnd != wnd)
        resetRenderState();

    if (!initialized)
    {
        ImGui_ImplX11_Init(display, (void*)wnd);
        game_wnd = wnd;

        initialized = true;
    }

    ImGui_ImplX11_NewFrame();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
// X11 window hooks
bool IgnoreEvent(XEvent &event)
{
    switch(event.type)
    {
        // Keyboard
        case KeyPress: case KeyRelease:
        // MouseButton
        case ButtonPress: case ButtonRelease:
        // Mouse move
        case MotionNotify:
        // Copy to clipboard request
        case SelectionRequest:
            return true;
    }
    return false;
}

int X11_Hook::check_for_overlay(Display *d, int num_events)
{
    static Time prev_time = {};
    X11_Hook* inst = Inst();

    if( inst->initialized )
    {
        XEvent event;
        while(num_events)
        {
            bool skip_input = inst->key_combination_callback(false);

            XPeekEvent(d, &event);
            ImGui_ImplX11_EventHandler(event);

            // Is the event is a key press
            if (event.type == KeyPress)
            {
                // Tab is pressed and was not pressed before
                if (event.xkey.keycode == XKeysymToKeycode(d, XK_Tab) && event.xkey.state & ShiftMask)
                {
                    // if key TAB is held, don't make the overlay flicker :p
                    if (event.xkey.time != prev_time)
                    {
                        skip_input = true;
                        inst->key_combination_callback(true);
                    }
                }
            }
            else if(event.type == KeyRelease && event.xkey.keycode == XKeysymToKeycode(d, XK_Tab))
            {
                prev_time = event.xkey.time;
            }

            if (!skip_input || !IgnoreEvent(event))
            {
                if(num_events)
                    num_events = 1;
                break;
            }

            XNextEvent(d, &event);
            --num_events;
        }
    }
    return num_events;
}

int X11_Hook::MyXEventsQueued(Display *display, int mode)
{
    X11_Hook* inst = X11_Hook::Inst();

    int res = inst->XEventsQueued(display, mode);

    if( res )
    {
        res = inst->check_for_overlay(display, res);
    }

    return res;
}

int X11_Hook::MyXPending(Display* display)
{
    int res = Inst()->XPending(display);

    if( res )
    {
        res = Inst()->check_for_overlay(display, res);
    }

    return res;
}

/////////////////////////////////////////////////////////////////////////////////////

X11_Hook::X11_Hook() :
    initialized(false),
    hooked(false),
    game_wnd(0),
    XEventsQueued(nullptr),
    XPending(nullptr)
{
}

X11_Hook::~X11_Hook()
{
    SPDLOG_INFO("X11 Hook removed");

    resetRenderState();

    _inst = nullptr;
}

X11_Hook* X11_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new X11_Hook;

    return _inst;
}

const char* X11_Hook::get_lib_name() const
{
    return DLL_NAME;
}
