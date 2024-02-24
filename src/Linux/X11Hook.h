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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <X11/X.h> // XEvent types
#include <X11/Xlib.h> // XEvent structure
#include <X11/Xutil.h> // XEvent keysym

namespace InGameOverlay {

class X11Hook_t :
    public BaseHook_t
{
public:
    static constexpr const char* DLL_NAME = "libX11.so";

private:
    static X11Hook_t* _inst;

    // Variables
    bool _Hooked;
    bool _Initialized;
    Window _GameWnd;

    // In (bool): Is toggle wanted
    // Out(bool): Is the overlay visible, if true, inputs will be disabled
    std::function<void()> _KeyCombinationCallback;
    std::set<uint32_t> _NativeKeyCombination;
    Window _SavedRoot;
    Window _SavedChild;
    int _SavedCursorRX;
    int _SavedCursorRY;
    int _SavedCursorX;
    int _SavedCursorY;
    unsigned int _SavedMask;
    bool _KeyCombinationPushed;
    bool _ApplicationInputsHidden;
    bool _OverlayInputsHidden;

    // Functions
    X11Hook_t();
    int _CheckForOverlay(Display *d, int num_events);

    // Hook to X11 window messages
    decltype(::XQueryPointer)* _XQueryPointer;
    decltype(::XEventsQueued)* _XEventsQueued;
    decltype(::XPending)* _XPending;

    static Bool MyXQueryPointer(Display* display, Window w, Window* root_return, Window* child_return, int* root_x_return, int* root_y_return, int* win_x_return, int* win_y_return, unsigned int* mask_return);
    static int MyXEventsQueued(Display * display, int mode);
    static int MyXPending(Display* display);

public:
    std::string LibraryName;

    virtual ~X11Hook_t();

    void ResetRenderState();
    void SetInitialWindowSize(Display* display, Window wnd);
    bool PrepareForOverlay(Display *display, Window wnd);

    Window GetGameWnd() const{ return _GameWnd; }

    bool StartHook(std::function<void()>& key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static X11Hook_t* Inst();
    virtual const std::string& GetLibraryName() const;
};

}// namespace InGameOverlay