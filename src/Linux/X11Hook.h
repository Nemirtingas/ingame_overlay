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

#include "../RendererHookInternal.h"

#include <X11/X.h> // XEvent types
#include <X11/Xlib.h> // XEvent structure
#include <X11/Xutil.h> // XEvent keysym

#include <xcb/xcb.h>
#include <X11/keysym.h>

namespace InGameOverlay {

class X11Hook_t :
    public BaseHook_t
{
private:
    enum class X11HookMode_t
    {
        HookNone,
        HookXlib,
        HookXCB,
    };

    struct XCBEventDecision_t
    {
        bool consume = false;
        bool needsNextEvent = false;
    };

    struct XCBPendingEvent_t
    {
        xcb_generic_event_t* event = nullptr;
        XCBEventDecision_t decision{};
    };

    static X11Hook_t* _inst;

    // Variables
    bool _Hooked;
    bool _Initialized;
    X11HookMode_t _X11HookMode;

    // Xlib mode
    bool _XlibLoadHook();
    bool _XlibStartHook();
    void _XlibResetRenderState(OverlayHookState state);
    bool _XlibSetInitialWindowSize(Display* display, Window wnd);
    bool _XlibPrepareForOverlay(Display* display, Window wnd);

    // XCB mode
    bool _XCBLoadHook();
    bool _XCBStartHook();
    void _XCBResetRenderState(OverlayHookState state);
    bool _XCBSetInitialWindowSize(xcb_connection_t* xcbConnection, xcb_window_t wnd);
    bool _XCBPrepareForOverlay(xcb_connection_t* xcbConnection, xcb_window_t wnd);
    xcb_connection_t* (*_XGetXCBConnection)(Display* display);
    XCBPendingEvent_t _XCBPendingEvent;
    uint32_t _XCBLastKeyReleaseTime;

    xcb_connection_t* _GetXCBConnection(Display* display);
    bool _IsKeyCombinationPressed() const;

    // In (bool): Is toggle wanted
    // Out(bool): Is the overlay visible, if true, inputs will be disabled
    std::function<void()> _KeyCombinationCallback;
    std::vector<ToggleKey> _OverlayToggleKeys;
    std::vector<uint32_t> _NativeKeyCombination;
    std::vector<uint32_t> _PressedKeycodes;
    uint32_t _GameWnd;
    bool _HasSavedCursor;
    uint32_t _SavedRoot;
    uint32_t _SavedChild;
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

    int _XlibCheckForOverlay(Display *d, int num_events);
    XCBEventDecision_t _XCBCheckForOverlay(xcb_connection_t* xcbConnection, xcb_generic_event_t* event, xcb_generic_event_t* nextEvent, bool isNextEvent);

    // Hook to X11 window messages
    decltype(::XQueryPointer)* _XQueryPointer;
    decltype(::XEventsQueued)* _XEventsQueued;
    decltype(::XPending)* _XPending;

    static Bool MyXQueryPointer(Display* display, Window w, Window* root_return, Window* child_return, int* root_x_return, int* root_y_return, int* win_x_return, int* win_y_return, unsigned int* mask_return);
    static int MyXEventsQueued(Display * display, int mode);
    static int MyXPending(Display* display);

    // Hook to XCB window messages
    decltype(::xcb_poll_for_event)* _XCBPollForEvent;
    decltype(::xcb_query_pointer_reply)* _XCBQueryPointerReply;

    static xcb_generic_event_t* MyXCBPollForEvent(xcb_connection_t* connection);
    static xcb_query_pointer_reply_t* MyXCBQueryPointerReply(xcb_connection_t* connection, xcb_query_pointer_cookie_t cookie, xcb_generic_error_t** error);

public:
    struct X11WindowEnumerationResult_t
    {
        inline X11WindowEnumerationResult_t() = default;
        inline X11WindowEnumerationResult_t(Display* display, Window window)
            : DisplayHandle(display)
            , WindowHandle(window)
        { }

        Display* DisplayHandle;
        Window WindowHandle;
    };

    std::string LibraryName;

    virtual ~X11Hook_t();

    void ResetRenderState(OverlayHookState state);
    bool SetInitialWindowSize(Display* display, Window wnd);
    bool PrepareForOverlay(Display* display, Window wnd);
    std::vector<X11WindowEnumerationResult_t> FindApplicationX11Window(int32_t processId);

    bool StartHook(std::function<void()>& keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static X11Hook_t* Inst();
    virtual const char* GetLibraryName() const;
};

}// namespace InGameOverlay