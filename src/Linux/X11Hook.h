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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#include <X11/X.h> // XEvent types
#include <X11/Xlib.h> // XEvent structure
#include <X11/Xutil.h> // XEvent keysym

#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xcb_keysyms.h>

namespace InGameOverlay {

class X11Hook_t :
    public BaseHook_t
{
private:
    static X11Hook_t* _inst;

    // Variables
    bool _Hooked;
    bool _Initialized;
    Display* _Display;
    Window _GameWnd;

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
    bool _UsesXcb;
    bool _IsWaitingReply;
    std::map<xcb_connection_t*, xcb_generic_event_t*> _NextConnectionEvent;

    // Functions
    X11Hook_t();
    int _CheckForOverlay(Display *d, int num_events);
    xcb_generic_event_t* _XcbCheckForOverlay(xcb_connection_t* xcbConnection, xcb_generic_event_t* event);
    bool _StartX11Hook();
    void _StartXcbHook();

    xcb_connection_t* (*_XGetXCBConnection)(Display* dpy);

    // Hook to X11 window messages
    decltype(::XQueryPointer)* _XQueryPointer;
    decltype(::XEventsQueued)* _XEventsQueued;
    decltype(::XPending)* _XPending;
    decltype(::xcb_poll_for_event)* _XcbPollForEvent;
    decltype(::xcb_send_request_with_fds64)* _XcbSendRequestWithFds64;
    decltype(::xcb_wait_for_reply)* _XcbWaitForReply;
    decltype(::xcb_wait_for_reply64)* _XcbWaitForReply64;

    static Bool MyXQueryPointer(Display* display, Window w, Window* root_return, Window* child_return, int* root_x_return, int* root_y_return, int* win_x_return, int* win_y_return, unsigned int* mask_return);
    static int MyXEventsQueued(Display * display, int mode);
    static int MyXPending(Display* display);
    static xcb_generic_event_t* MyXcbPollForEvent(xcb_connection_t* c);
    static uint64_t MyXcbSendRequestWithFds64(xcb_connection_t* c, int flags, struct iovec* vector,
        const xcb_protocol_request_t* req, unsigned int num_fds, int* fds);
    static void* MyXcbWaitForReply(xcb_connection_t* c, unsigned int request, xcb_generic_error_t** e);
    static void* MyXcbWaitForReply64(xcb_connection_t* c, uint64_t request, xcb_generic_error_t** e);

public:
    std::string LibraryName;

    virtual ~X11Hook_t();

    void ResetRenderState(OverlayHookState state);
    bool SetInitialWindowSize(Window wnd);
    bool PrepareForOverlay(Window wnd);
    std::vector<Window> FindApplicationX11Window(int32_t processId);

    Window GetGameWnd() const{ return _GameWnd; }

    bool StartHook(std::function<void()>& key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static X11Hook_t* Inst();
    virtual const std::string& GetLibraryName() const;
};

}// namespace InGameOverlay