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

#pragma once

#include "../Base_Hook.h"

#include <X11/X.h> // XEvent types
#include <X11/Xlib.h> // XEvent structure
#include <X11/Xutil.h> // XEvent keysym

class X11_Hook : public Base_Hook
{
public:
    static constexpr const char* DLL_NAME = "libX11.so";

private:
    static X11_Hook* _inst;

    // Variables
    bool hooked;
    bool initialized;
    Window game_wnd;

    // Functions
    X11_Hook();
    int check_for_overlay(Display *d, int num_events);

    // Hook to X11 window messages
    decltype(::XEventsQueued)* XEventsQueued;
    decltype(::XPending)* XPending;

    static int MyXEventsQueued(Display * display, int mode);
    static int MyXPending(Display* display);

    std::function<bool(bool)> key_combination_callback;

public:
    virtual ~X11_Hook();

    void resetRenderState();
    bool prepareForOverlay(Display *display, Window wnd);

    Window get_game_wnd() const{ return game_wnd; }

    bool start_hook(std::function<bool(bool)>& key_combination_callback);
    static X11_Hook* Inst();
    virtual const char* get_lib_name() const;
    void loadFunctions(decltype(::XEventsQueued)* pfnXEventsQueued, decltype(::XPending)* pfnXPending);

};
