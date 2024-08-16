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

#include "X11Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_x11.h>
#include <System/Library.h>

extern int ImGui_ImplX11_EventHandler(XEvent& event, XEvent* nextEvent);

namespace InGameOverlay {

static constexpr const char X11_DLL_NAME[] = "libX11.so";
static constexpr const char X11_XCB_DLL_NAME[] = "libX11-xcb.so";
static constexpr const char XCB_DLL_NAME[] = "libxcb.so";

X11Hook_t* X11Hook_t::_inst = nullptr;

static std::shared_ptr<Display> GetX11Display()
{
    auto displayHandle = XOpenDisplay(nullptr);
    if (displayHandle == nullptr)
        return std::shared_ptr<Display>(nullptr);

    return std::shared_ptr<Display>(displayHandle, [](Display* handle)
    {
        if (handle != nullptr)
            XCloseDisplay(handle);
    });
}

typedef int (*EnumX11WindowsCallback_t)(Display* display, Window window, void* userParameter);

static void RunEnumX11Windows(Display* display, Window rootWindow, EnumX11WindowsCallback_t callback, void* userParameter)
{
    Window parentWindow;
    Window* childrenWindows;
    Window* child;
    unsigned int childCount;

    if (XQueryTree(display, rootWindow, &rootWindow, &parentWindow, &childrenWindows, &childCount) && childCount)
    {
        for (unsigned int i = 0; i < childCount; ++i)
        {
            if (!callback(display, childrenWindows[i], userParameter))
                return;

            RunEnumX11Windows(display, childrenWindows[i], callback, userParameter);
        }
    }
}

static void EnumX11Windows(EnumX11WindowsCallback_t callback, void* userParameter, Display* display = nullptr)
{
    std::shared_ptr<Display> localDisplay;
    if (display == nullptr)
    {
        localDisplay = GetX11Display();
        display = localDisplay.get();
    }

    if (display == nullptr)
        return;

    Window rootWindow = DefaultRootWindow(display);
    if (rootWindow == None || !callback(display, rootWindow, userParameter))
        return;

    RunEnumX11Windows(display, rootWindow, callback, userParameter);
}

static uint32_t ToggleKeyToNativeKey(InGameOverlay::ToggleKey k)
{
    struct {
        InGameOverlay::ToggleKey lib_key;
        uint32_t native_key;
    } mapping[] = {
        { InGameOverlay::ToggleKey::ALT  , XK_Alt_L     },
        { InGameOverlay::ToggleKey::CTRL , XK_Control_L },
        { InGameOverlay::ToggleKey::SHIFT, XK_Shift_L   },
        { InGameOverlay::ToggleKey::TAB  , XK_Tab       },
        { InGameOverlay::ToggleKey::F1   , XK_F1        },
        { InGameOverlay::ToggleKey::F2   , XK_F2        },
        { InGameOverlay::ToggleKey::F3   , XK_F3        },
        { InGameOverlay::ToggleKey::F4   , XK_F4        },
        { InGameOverlay::ToggleKey::F5   , XK_F5        },
        { InGameOverlay::ToggleKey::F6   , XK_F6        },
        { InGameOverlay::ToggleKey::F7   , XK_F7        },
        { InGameOverlay::ToggleKey::F8   , XK_F8        },
        { InGameOverlay::ToggleKey::F9   , XK_F9        },
        { InGameOverlay::ToggleKey::F10  , XK_F10       },
        { InGameOverlay::ToggleKey::F11  , XK_F11       },
        { InGameOverlay::ToggleKey::F12  , XK_F12       },
    };

    for (auto const& item : mapping)
    {
        if (item.lib_key == k)
            return item.native_key;
    }

    return 0;
}

static inline bool GetKeyState(Display* d, KeySym keySym, char szKey[32])
{
    int iKeyCodeToFind = XKeysymToKeycode(d, keySym);

    return szKey[iKeyCodeToFind / 8] & (1 << (iKeyCodeToFind % 8));
}

static bool XcbGetKeyState(xcb_connection_t* xcbConnection, int keySym, uint8_t szKey[32])
{
    xcb_key_symbols_t* keySymbols = xcb_key_symbols_alloc(xcbConnection);

    xcb_keycode_t* keyCodeToFind = xcb_key_symbols_get_keycode(keySymbols, keySym);
    int iKeyCodeToFind = keyCodeToFind == nullptr ? 0 : *keyCodeToFind;

    xcb_key_symbols_free(keySymbols);

    bool r = iKeyCodeToFind == 0 ? false : szKey[iKeyCodeToFind / 8] & (1 << (iKeyCodeToFind % 8));

    free(keyCodeToFind);

    return r;
}

bool X11Hook_t::_StartX11Hook()
{
    void* hX11 = System::Library::GetLibraryHandle(X11_DLL_NAME);
    if (hX11 == nullptr)
    {
        INGAMEOVERLAY_WARN("Failed to hook X11: Cannot find {}", X11_DLL_NAME);
        return false;
    }

    System::Library::Library libX11;
    LibraryName = System::Library::GetLibraryPath(hX11);

    if (!libX11.OpenLibrary(LibraryName, false))
    {
        INGAMEOVERLAY_WARN("Failed to hook X11: Cannot load {}", LibraryName);
        return false;
    }

    struct {
        void** func_ptr;
        void* hook_ptr;
        const char* func_name;
    } hook_array[] = {
        { (void**)&_XGetXCBConnection, nullptr                           , "XGetXCBConnection" },
        { (void**)&_XEventsQueued    , (void*)&X11Hook_t::MyXEventsQueued, "XEventsQueued" },
        { (void**)&_XPending         , (void*)&X11Hook_t::MyXPending     , "XPending"      },
        { (void**)&_XQueryPointer    , (void*)&X11Hook_t::MyXQueryPointer, "XQueryPointer" },
    };

    for (auto& entry : hook_array)
    {
        *entry.func_ptr = libX11.GetSymbol<void*>(entry.func_name);
        if (entry.func_ptr == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: Event function {} missing.", entry.func_name);
            return false;
        }
    }

    BeginHook();

    for (auto& entry : hook_array)
    {
        if (entry.hook_ptr != nullptr)
        {
            if (!HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr)))
            {
                INGAMEOVERLAY_ERROR("Failed to hook {}", entry.func_name);
            }
        }
    }

    EndHook();
    
    if (_XGetXCBConnection == nullptr)
    {
        void* hX11xcb = System::Library::GetLibraryHandle(X11_XCB_DLL_NAME);
        if (hX11xcb != nullptr)
            _XGetXCBConnection = (decltype(_XGetXCBConnection))System::Library::GetSymbol(hX11xcb, "XGetXCBConnection");
    }

    if (_XGetXCBConnection != nullptr)
    {
        INGAMEOVERLAY_INFO("libX11 seems to use xcb.");
    }

    return true;
}

void X11Hook_t::_StartXcbHook()
{
    void* hXcb = System::Library::GetLibraryHandle(XCB_DLL_NAME);
    if (hXcb == nullptr)
    {
        INGAMEOVERLAY_INFO("Failed to hook xcb: Cannot find {}", XCB_DLL_NAME);
        return;
    }

    System::Library::Library libXcb;
    auto xcbPath = System::Library::GetLibraryPath(hXcb);

    if (!libXcb.OpenLibrary(xcbPath, false))
    {
        INGAMEOVERLAY_INFO("Failed to hook xcb: Cannot load {}", xcbPath);
        return;
    }

    struct {
        void** func_ptr;
        void* hook_ptr;
        const char* func_name;
    } hook_array[] = {
        { (void**)&_XcbPollForEvent        , (void*)&X11Hook_t::MyXcbPollForEvent        , "xcb_poll_for_event"          },
        { (void**)&_XcbSendRequestWithFds64, (void*)&X11Hook_t::MyXcbSendRequestWithFds64, "xcb_send_request_with_fds64" },
        { (void**)&_XcbWaitForReply        , (void*)&X11Hook_t::MyXcbWaitForReply        , "xcb_wait_for_reply"          },
        { (void**)&_XcbWaitForReply64      , (void*)&X11Hook_t::MyXcbWaitForReply64      , "xcb_wait_for_reply64"        },
    };

    for (auto& entry : hook_array)
    {
        *entry.func_ptr = libXcb.GetSymbol<void*>(entry.func_name);
        if (entry.func_ptr == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook xcb: Event function {} missing.", entry.func_name);
            return;
        }
    }

    BeginHook();

    for (auto& entry : hook_array)
    {
        if (!HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr)))
        {
            INGAMEOVERLAY_ERROR("Failed to hook {}", entry.func_name);
        }
    }

    EndHook();

    INGAMEOVERLAY_INFO("Hooked xcb");
}

bool X11Hook_t::StartHook(std::function<void()>& _key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys)
{
    if (!_Hooked)
    {
        if (!_key_combination_callback)
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: No key combination callback.");
            return false;
        }

        if (toggle_keys.empty())
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: No key combination.");
            return false;
        }

        if (!_StartX11Hook())
            return false;
        _StartXcbHook();

        INGAMEOVERLAY_INFO("Hooked X11");

        _KeyCombinationCallback = std::move(_key_combination_callback);
        
        for (auto& key : toggle_keys)
        {
            uint32_t k = ToggleKeyToNativeKey(key);
            if (k != 0)
            {
                _NativeKeyCombination.insert(k);
            }
        }

        _Hooked = true;
    }
    return true;
}

void X11Hook_t::HideAppInputs(bool hide)
{
    _ApplicationInputsHidden = hide;
}

void X11Hook_t::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void X11Hook_t::ResetRenderState(OverlayHookState state)
{
    if (!_Initialized)
        return;

    _Display = nullptr;
    _GameWnd = 0;

    HideAppInputs(false);
    HideOverlayInputs(true);

    ImGui_ImplX11_Shutdown();
    _Initialized = false;
}

bool X11Hook_t::SetInitialWindowSize(Window wnd)
{
    if (_Display == nullptr)
        return false;

    unsigned int width, height;
    Window unused_window;
    int unused_int;
    unsigned int unused_unsigned_int;

    XGetGeometry(_Display, wnd, &unused_window, &unused_int, &unused_int, &width, &height, &unused_unsigned_int, &unused_unsigned_int);

    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
    return true;
}

bool X11Hook_t::PrepareForOverlay(Window wnd)
{
    if(!_Hooked || _Display == nullptr)
        return false;

    if (_GameWnd != wnd)
        ResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        ImGui_ImplX11_Init(_Display, (void*)wnd, (void*)_XQueryPointer);
        _GameWnd = wnd;

        //XSelectInput(display,
        //    wnd,
        //    SubstructureRedirectMask | SubstructureNotifyMask |
        //    KeyPressMask | KeyReleaseMask |
        //    ButtonPressMask | ButtonReleaseMask |
        //    FocusChangeMask | ExposureMask);

        _Initialized = true;
    }

    if (!_OverlayInputsHidden)
    {
        ImGui_ImplX11_NewFrame();
    }

    return true;
}

std::vector<Window> X11Hook_t::FindApplicationX11Window(int32_t processId)
{
    struct
    {
        int32_t pid;
        std::vector<Window> windows;
        Atom pidAtom;
    } windowParams{
        processId,
        {},
        None
    };

    EnumX11Windows([](Display* display, Window window, void* userParameter) -> int
    {
        auto params = reinterpret_cast<decltype(windowParams)*>(userParameter);
        if (params->pidAtom == None)
            params->pidAtom = XInternAtom(display, "_NET_WM_PID", True);

        if (params->pidAtom == None)
            return 0;

        XTextProperty data;
        int status = XGetTextProperty(display, window, &data, params->pidAtom);
        if (!status || data.nitems <= 0)
            return 1;

        int32_t processId = 0;
        switch (data.format)
        {
            case 32: processId = *(int32_t*)data.value; break;
            case 16: processId = *(int16_t*)data.value; break;
            case 8 : processId = data.value[0]; break;
            default: return 1;
        }

        if (processId == params->pid)
            params->windows.emplace_back(window);

        return 1;
    }, &windowParams);

    return windowParams.windows;
}

/////////////////////////////////////////////////////////////////////////////////////
// X11 window hooks
static bool IgnoreXEvent(XEvent &event)
{
    switch(event.type)
    {
        // Keyboard
        case KeyPress: case KeyRelease:
        // Mouse button
        case ButtonPress: case ButtonRelease:
        // Mouse move
        case MotionNotify:
        // Copy to clipboard request
        case SelectionRequest:
            return true;
    }
    return false;
}

static bool IgnoreXcbEvent(xcb_generic_event_t* event)
{
    switch (event->response_type)
    {
        // Keyboard
        case XCB_KEY_PRESS: case XCB_KEY_RELEASE:
        // Mouse button
        case XCB_BUTTON_PRESS: case XCB_BUTTON_RELEASE:
        // Mouse move
        case XCB_MOTION_NOTIFY:
        // Copy to clipboard request
        case XCB_SELECTION_REQUEST:
            return true;
    }

    return false;
}

int X11Hook_t::_CheckForOverlay(Display *d, int num_events)
{
    char szKey[32];

    if (!_Initialized)
        return num_events;

    XEvent event, nextEvent;
    XEvent* pNextEvent;
    while(num_events)
    {
        bool hide_app_inputs = _ApplicationInputsHidden;
        bool hide_overlay_inputs = _OverlayInputsHidden;

        XPeekEvent(d, &event);

        if (event.type == KeyRelease && num_events > 1)
        {
            XNextEvent(d, &event);
            XPeekEvent(d, &nextEvent);
            XPutBackEvent(d, &event);
            pNextEvent = &nextEvent;
            // Consume only 1 event because we don't want to send the KeyRelease event
            // but we still want to send the KeyPress event.
        }
        else
        {
            pNextEvent = nullptr;
        }

        // Is the event is a key press
        if (event.type == KeyPress || event.type == KeyRelease)
        {
            XQueryKeymap(d, szKey);
            int key_count = 0;
            for (auto const& key : _NativeKeyCombination)
            {
                if (GetKeyState(d, key, szKey))
                    ++key_count;
            }

            if (key_count == _NativeKeyCombination.size())
            {// All shortcut keys are pressed
                if (!_KeyCombinationPushed)
                {
                    _KeyCombinationCallback();

                    if (_OverlayInputsHidden)
                        hide_overlay_inputs = true;

                    if (_ApplicationInputsHidden)
                    {
                        hide_app_inputs = true;

                        // Save the last known cursor pos when opening the overlay
                        // so we can spoof the XQueryPointer return value.
                        _XQueryPointer(d, _GameWnd, &_SavedRoot, &_SavedChild, &_SavedCursorRX, &_SavedCursorRY, &_SavedCursorX, &_SavedCursorY, &_SavedMask);
                    }

                    _KeyCombinationPushed = true;
                }
            }
            else
            {
                _KeyCombinationPushed = false;
            }
        }

        if (!hide_overlay_inputs || event.type == FocusIn || event.type == FocusOut)
        {
            ImGui_ImplX11_EventHandler(event, pNextEvent);
        }

        if (!hide_app_inputs || !IgnoreXEvent(event))
        {
            if(num_events)
                num_events = 1;
            break;
        }

        XNextEvent(d, &event);
        --num_events;
    }
    return num_events;
}

xcb_generic_event_t* X11Hook_t::_XcbCheckForOverlay(xcb_connection_t* xcbConnection, xcb_generic_event_t* xcbEvent)
{
    INGAMEOVERLAY_INFO("");
    if (!_Initialized)
        return xcbEvent;

    INGAMEOVERLAY_INFO("");
    bool hide_app_inputs = _ApplicationInputsHidden;
    bool hide_overlay_inputs = _OverlayInputsHidden;
    xcb_generic_event_t* nextEvent = nullptr;

    if (xcbEvent->response_type == XCB_KEY_RELEASE)
    {
        nextEvent = _XcbPollForEvent(xcbConnection);
        if (nextEvent != nullptr)
            _NextConnectionEvent[xcbConnection] = nextEvent;
    }

    // Is the event is a key press
    if (xcbEvent->response_type == XCB_KEY_PRESS || xcbEvent->response_type == XCB_KEY_RELEASE)
    {
        // XCB keyEvent->detail == Xlib event.xkey.keycode
        // XCB keyEvent->state  == Xlib event.xkey.state

        xcb_query_keymap_reply_t* keymap = xcb_query_keymap_reply(xcbConnection, xcb_query_keymap(xcbConnection), NULL);
        if (keymap != nullptr)
        {
            int key_count = 0;
            for (auto const& key : _NativeKeyCombination)
            {
                if (XcbGetKeyState(xcbConnection, key, keymap->keys))
                    ++key_count;
            }

            free(keymap);

            if (key_count == _NativeKeyCombination.size())
            {// All shortcut keys are pressed
                if (!_KeyCombinationPushed)
                {
                    _KeyCombinationCallback();

                    if (_OverlayInputsHidden)
                        hide_overlay_inputs = true;

                    if (_ApplicationInputsHidden)
                    {
                        hide_app_inputs = true;

                        // Save the last known cursor pos when opening the overlay
                        // so we can spoof the xcb_query_pointer return value.
                        //if (_XcbSavedPointerReply != nullptr)
                        //    free(_XcbSavedPointerReply);

                        //xcb_query_pointer_cookie_t pointerCookie = xcb_query_pointer(xcbConnection, xcb_setup_roots_iterator(xcb_get_setup(xcbConnection)).data->root);
                        //_XcbSavedPointerReply = _XcbQueryPointerReply(xcbConnection, pointerCookie, nullptr);
                    }

                    _KeyCombinationPushed = true;
                }
            }
            else
            {
                _KeyCombinationPushed = false;
            }
        }


        if (!hide_overlay_inputs || xcbEvent->response_type == XCB_FOCUS_IN || xcbEvent->response_type == XCB_FOCUS_OUT)
        {
            //ImGui_ImplX11_EventHandler(xcbEvent, pNextEvent);
        }

        if (!hide_app_inputs || !IgnoreXcbEvent(xcbEvent))
            return xcbEvent;

        return nullptr;
    }

    return xcbEvent;
}

Bool X11Hook_t::MyXQueryPointer(Display* display, Window w, Window* root_return, Window* child_return, int* root_x_return, int* root_y_return, int* win_x_return, int* win_y_return, unsigned int* mask_return)
{
    X11Hook_t* inst = X11Hook_t::Inst();

    Bool res = inst->_XQueryPointer(display, w, root_return, child_return, root_x_return, root_y_return, win_x_return, win_y_return, mask_return);
    if (inst->_Initialized && inst->_ApplicationInputsHidden)
    {
        if (root_return   != nullptr) *root_return   = inst->_SavedRoot;
        if (child_return  != nullptr) *child_return  = inst->_SavedChild;
        if (root_x_return != nullptr) *root_x_return = inst->_SavedCursorRX;
        if (root_y_return != nullptr) *root_y_return = inst->_SavedCursorRY;
        if (win_x_return  != nullptr) *win_x_return  = inst->_SavedCursorX;
        if (win_y_return  != nullptr) *win_y_return  = inst->_SavedCursorY;
        if (mask_return   != nullptr) *mask_return   = inst->_SavedMask;
    }

    return res;
}

int X11Hook_t::MyXEventsQueued(Display *display, int mode)
{
    X11Hook_t* inst = X11Hook_t::Inst();

    auto res = inst->_XEventsQueued(display, mode);

    if (!inst->_UsesXcb && res)
    {
        inst->_Display = display;
        res = inst->_CheckForOverlay(display, res);
    }

    return res;
}

int X11Hook_t::MyXPending(Display* display)
{
    X11Hook_t* inst = X11Hook_t::Inst();

    auto res = inst->_XPending(display);

    if (!inst->_UsesXcb && res)
    {
        inst->_Display = display;
        res = inst->_CheckForOverlay(display, res);
    }

    if (res)
        res = inst->_CheckForOverlay(display, res);

    return res;
}

xcb_generic_event_t* X11Hook_t::MyXcbPollForEvent(xcb_connection_t* c)
{
    INGAMEOVERLAY_INFO("xcb_poll_for_event ENTER");
    X11Hook_t* inst = X11Hook_t::Inst();

    inst->_UsesXcb = true;

    if (inst->_NextConnectionEvent[c] != nullptr)
    {
        auto* xcbEvent = inst->_NextConnectionEvent[c];
        inst->_NextConnectionEvent[c] = nullptr;
        return xcbEvent;
    }

    auto* xcbEvent = inst->_XcbPollForEvent(c);

    if (xcbEvent != nullptr)
    {
        INGAMEOVERLAY_INFO("");
        if (inst->_XcbCheckForOverlay(c, xcbEvent) == nullptr && !inst->_IsWaitingReply)
        {
            free(xcbEvent);
            xcbEvent = nullptr;
        }
    }

    INGAMEOVERLAY_INFO("xcb_poll_for_event EXIT");
    return xcbEvent;
}

static std::string request_to_string(uint8_t opcode)
{
#define OPCODE_TO_STRING(VALUE) case VALUE: return #VALUE
    switch (opcode)
    {
        OPCODE_TO_STRING(XCB_CREATE_WINDOW);
        OPCODE_TO_STRING(XCB_CHANGE_WINDOW_ATTRIBUTES);
        OPCODE_TO_STRING(XCB_GET_WINDOW_ATTRIBUTES);
        OPCODE_TO_STRING(XCB_DESTROY_WINDOW);
        OPCODE_TO_STRING(XCB_DESTROY_SUBWINDOWS);
        OPCODE_TO_STRING(XCB_CHANGE_SAVE_SET);
        OPCODE_TO_STRING(XCB_REPARENT_WINDOW);
        OPCODE_TO_STRING(XCB_MAP_WINDOW);
        OPCODE_TO_STRING(XCB_MAP_SUBWINDOWS);
        OPCODE_TO_STRING(XCB_UNMAP_WINDOW);
        OPCODE_TO_STRING(XCB_UNMAP_SUBWINDOWS);
        OPCODE_TO_STRING(XCB_CONFIGURE_WINDOW);
        OPCODE_TO_STRING(XCB_CIRCULATE_WINDOW);
        OPCODE_TO_STRING(XCB_GET_GEOMETRY);
        OPCODE_TO_STRING(XCB_QUERY_TREE);
        OPCODE_TO_STRING(XCB_INTERN_ATOM);
        OPCODE_TO_STRING(XCB_GET_ATOM_NAME);
        OPCODE_TO_STRING(XCB_CHANGE_PROPERTY);
        OPCODE_TO_STRING(XCB_DELETE_PROPERTY);
        OPCODE_TO_STRING(XCB_GET_PROPERTY);
        OPCODE_TO_STRING(XCB_LIST_PROPERTIES);
        OPCODE_TO_STRING(XCB_SET_SELECTION_OWNER);
        OPCODE_TO_STRING(XCB_GET_SELECTION_OWNER);
        OPCODE_TO_STRING(XCB_CONVERT_SELECTION);
        OPCODE_TO_STRING(XCB_SEND_EVENT);
        OPCODE_TO_STRING(XCB_GRAB_POINTER);
        OPCODE_TO_STRING(XCB_UNGRAB_POINTER);
        OPCODE_TO_STRING(XCB_GRAB_BUTTON);
        OPCODE_TO_STRING(XCB_UNGRAB_BUTTON);
        OPCODE_TO_STRING(XCB_CHANGE_ACTIVE_POINTER_GRAB);
        OPCODE_TO_STRING(XCB_GRAB_KEYBOARD);
        OPCODE_TO_STRING(XCB_UNGRAB_KEYBOARD);
        OPCODE_TO_STRING(XCB_GRAB_KEY);
        OPCODE_TO_STRING(XCB_UNGRAB_KEY);
        OPCODE_TO_STRING(XCB_ALLOW_EVENTS);
        OPCODE_TO_STRING(XCB_GRAB_SERVER);
        OPCODE_TO_STRING(XCB_UNGRAB_SERVER);
        OPCODE_TO_STRING(XCB_QUERY_POINTER);
        OPCODE_TO_STRING(XCB_GET_MOTION_EVENTS);
        OPCODE_TO_STRING(XCB_TRANSLATE_COORDINATES);
        OPCODE_TO_STRING(XCB_WARP_POINTER);
        OPCODE_TO_STRING(XCB_SET_INPUT_FOCUS);
        OPCODE_TO_STRING(XCB_GET_INPUT_FOCUS);
        OPCODE_TO_STRING(XCB_QUERY_KEYMAP);
        OPCODE_TO_STRING(XCB_OPEN_FONT);
        OPCODE_TO_STRING(XCB_CLOSE_FONT);
        OPCODE_TO_STRING(XCB_QUERY_FONT);
        OPCODE_TO_STRING(XCB_QUERY_TEXT_EXTENTS);
        OPCODE_TO_STRING(XCB_LIST_FONTS);
        OPCODE_TO_STRING(XCB_LIST_FONTS_WITH_INFO);
        OPCODE_TO_STRING(XCB_SET_FONT_PATH);
        OPCODE_TO_STRING(XCB_GET_FONT_PATH);
        OPCODE_TO_STRING(XCB_CREATE_PIXMAP);
        OPCODE_TO_STRING(XCB_FREE_PIXMAP);
        OPCODE_TO_STRING(XCB_CREATE_GC);
        OPCODE_TO_STRING(XCB_CHANGE_GC);
        OPCODE_TO_STRING(XCB_COPY_GC);
        OPCODE_TO_STRING(XCB_SET_DASHES);
        OPCODE_TO_STRING(XCB_SET_CLIP_RECTANGLES);
        OPCODE_TO_STRING(XCB_FREE_GC);
        OPCODE_TO_STRING(XCB_CLEAR_AREA);
        OPCODE_TO_STRING(XCB_COPY_AREA);
        OPCODE_TO_STRING(XCB_COPY_PLANE);
        OPCODE_TO_STRING(XCB_POLY_POINT);
        OPCODE_TO_STRING(XCB_POLY_LINE);
        OPCODE_TO_STRING(XCB_POLY_SEGMENT);
        OPCODE_TO_STRING(XCB_POLY_RECTANGLE);
        OPCODE_TO_STRING(XCB_POLY_ARC);
        OPCODE_TO_STRING(XCB_FILL_POLY);
        OPCODE_TO_STRING(XCB_POLY_FILL_RECTANGLE);
        OPCODE_TO_STRING(XCB_POLY_FILL_ARC);
        OPCODE_TO_STRING(XCB_PUT_IMAGE);
        OPCODE_TO_STRING(XCB_GET_IMAGE);
        OPCODE_TO_STRING(XCB_POLY_TEXT_8);
        OPCODE_TO_STRING(XCB_POLY_TEXT_16);
        OPCODE_TO_STRING(XCB_IMAGE_TEXT_8);
        OPCODE_TO_STRING(XCB_IMAGE_TEXT_16);
        OPCODE_TO_STRING(XCB_CREATE_COLORMAP);
        OPCODE_TO_STRING(XCB_FREE_COLORMAP);
        OPCODE_TO_STRING(XCB_COPY_COLORMAP_AND_FREE);
        OPCODE_TO_STRING(XCB_INSTALL_COLORMAP);
        OPCODE_TO_STRING(XCB_UNINSTALL_COLORMAP);
        OPCODE_TO_STRING(XCB_LIST_INSTALLED_COLORMAPS);
        OPCODE_TO_STRING(XCB_ALLOC_COLOR);
        OPCODE_TO_STRING(XCB_ALLOC_NAMED_COLOR);
        OPCODE_TO_STRING(XCB_ALLOC_COLOR_CELLS);
        OPCODE_TO_STRING(XCB_ALLOC_COLOR_PLANES);
        OPCODE_TO_STRING(XCB_FREE_COLORS);
        OPCODE_TO_STRING(XCB_STORE_COLORS);
        OPCODE_TO_STRING(XCB_STORE_NAMED_COLOR);
        OPCODE_TO_STRING(XCB_QUERY_COLORS);
        OPCODE_TO_STRING(XCB_LOOKUP_COLOR);
        OPCODE_TO_STRING(XCB_CREATE_CURSOR);
        OPCODE_TO_STRING(XCB_CREATE_GLYPH_CURSOR);
        OPCODE_TO_STRING(XCB_FREE_CURSOR);
        OPCODE_TO_STRING(XCB_RECOLOR_CURSOR);
        OPCODE_TO_STRING(XCB_QUERY_BEST_SIZE);
        OPCODE_TO_STRING(XCB_QUERY_EXTENSION);
        OPCODE_TO_STRING(XCB_LIST_EXTENSIONS);
        OPCODE_TO_STRING(XCB_CHANGE_KEYBOARD_MAPPING);
        OPCODE_TO_STRING(XCB_GET_KEYBOARD_MAPPING);
        OPCODE_TO_STRING(XCB_CHANGE_KEYBOARD_CONTROL);
        OPCODE_TO_STRING(XCB_GET_KEYBOARD_CONTROL);
        OPCODE_TO_STRING(XCB_BELL);
        OPCODE_TO_STRING(XCB_CHANGE_POINTER_CONTROL);
        OPCODE_TO_STRING(XCB_GET_POINTER_CONTROL);
        OPCODE_TO_STRING(XCB_SET_SCREEN_SAVER);
        OPCODE_TO_STRING(XCB_GET_SCREEN_SAVER);
        OPCODE_TO_STRING(XCB_CHANGE_HOSTS);
        OPCODE_TO_STRING(XCB_LIST_HOSTS);
        OPCODE_TO_STRING(XCB_SET_ACCESS_CONTROL);
        OPCODE_TO_STRING(XCB_SET_CLOSE_DOWN_MODE);
        OPCODE_TO_STRING(XCB_KILL_CLIENT);
        OPCODE_TO_STRING(XCB_ROTATE_PROPERTIES);
        OPCODE_TO_STRING(XCB_FORCE_SCREEN_SAVER);
        OPCODE_TO_STRING(XCB_SET_POINTER_MAPPING);
        OPCODE_TO_STRING(XCB_GET_POINTER_MAPPING);
        OPCODE_TO_STRING(XCB_SET_MODIFIER_MAPPING);
        OPCODE_TO_STRING(XCB_GET_MODIFIER_MAPPING);
        OPCODE_TO_STRING(XCB_NO_OPERATION);
    }
#undef OPCODE_TO_STRING
    return std::to_string(opcode);
}

uint64_t X11Hook_t::MyXcbSendRequestWithFds64(xcb_connection_t* c, int flags, struct iovec* vector,
    const xcb_protocol_request_t* req, unsigned int num_fds, int* fds)
{
    INGAMEOVERLAY_INFO("xcb_send_request_with_fds64 ENTER: {}", request_to_string(req->opcode));
    X11Hook_t* inst = X11Hook_t::Inst();

    auto result = inst->_XcbSendRequestWithFds64(c, flags, vector, req, num_fds, fds);

    INGAMEOVERLAY_INFO("xcb_send_request_with_fds64 EXIT");
    return result;
}

void* X11Hook_t::MyXcbWaitForReply(xcb_connection_t* c, unsigned int request, xcb_generic_error_t** e)
{
    INGAMEOVERLAY_INFO("xcb_wait_for_reply ENTER");
    X11Hook_t* inst = X11Hook_t::Inst();

    inst->_IsWaitingReply = true;
    auto* result = inst->_XcbWaitForReply(c, request, e);
    inst->_IsWaitingReply = false;

    INGAMEOVERLAY_INFO("xcb_wait_for_reply EXIT");
    return result;
}

void* X11Hook_t::MyXcbWaitForReply64(xcb_connection_t* c, uint64_t request, xcb_generic_error_t** e)
{
    INGAMEOVERLAY_INFO("xcb_wait_for_reply64 ENTER");
    X11Hook_t* inst = X11Hook_t::Inst();

    inst->_IsWaitingReply = true;
    auto* result = inst->_XcbWaitForReply64(c, request, e);
    inst->_IsWaitingReply = false;

    INGAMEOVERLAY_INFO("xcb_wait_for_reply64 EXIT");
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////

X11Hook_t::X11Hook_t() :
    _Initialized(false),
    _Hooked(false),
    _Display(nullptr),
    _GameWnd(0),
    _KeyCombinationPushed(false),
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _UsesXcb(false),
    _IsWaitingReply(false),
    _XGetXCBConnection(nullptr),
    _XQueryPointer(nullptr),
    _XEventsQueued(nullptr),
    _XPending(nullptr),
    _XcbPollForEvent(nullptr),
    _XcbSendRequestWithFds64(nullptr),
    _XcbWaitForReply(nullptr),
    _XcbWaitForReply64(nullptr)
{
}

X11Hook_t::~X11Hook_t()
{
    INGAMEOVERLAY_INFO("X11 Hook removed");

    ResetRenderState(OverlayHookState::Removing);

    _inst = nullptr;
}

X11Hook_t* X11Hook_t::Inst()
{
    if (_inst == nullptr)
        _inst = new X11Hook_t;

    return _inst;
}

const std::string& X11Hook_t::GetLibraryName() const
{
    return LibraryName;
}

}// namespace InGameOverlay