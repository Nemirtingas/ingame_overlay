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

#undef Status

#include <xcb/xinput.h>

#include <imgui.h>
#include <backends/imgui_impl_x11.h>
#include <backends/imgui_impl_xcb.h>
#include <System/Library.h>

extern int ImGui_ImplX11_EventHandler(XEvent& event, XEvent* nextEvent);
extern int ImGui_ImplXCB_EventHandler(xcb_generic_event_t* event, xcb_generic_event_t* nextEvent);

namespace InGameOverlay {

static constexpr const char X11_DLL_NAME[] = "libX11.so";
static constexpr const char X11_XCB_DLL_NAME[] = "libX11-xcb.so";
static constexpr const char XCB_DLL_NAME[] = "libxcb.so";

X11Hook_t* X11Hook_t::_inst = nullptr;

static std::shared_ptr<SafeXlibDisplay_t> GetX11Display()
{
    auto displayHandle = XOpenDisplay(nullptr);
    if (displayHandle == nullptr)
        return std::make_shared<SafeXlibDisplay_t>();

    return std::make_shared<SafeXlibDisplay_t>(displayHandle);
}

typedef int (*EnumX11WindowsCallback_t)(std::shared_ptr<SafeXlibDisplay_t> display, Window window, void* userParameter);

static void RunEnumX11Windows(std::shared_ptr<SafeXlibDisplay_t> display, Window rootWindow, EnumX11WindowsCallback_t callback, void* userParameter)
{
    Window parentWindow;
    Window* childrenWindows;
    Window* child;
    unsigned int childCount;

    if (XQueryTree(static_cast<Display*>(display->DisplayHandle), rootWindow, &rootWindow, &parentWindow, &childrenWindows, &childCount) && childCount)
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
    std::shared_ptr<SafeXlibDisplay_t> localDisplay;
    if (display == nullptr)
    {
        localDisplay = GetX11Display();
        display = static_cast<Display*>(localDisplay->DisplayHandle);
    }

    if (display == nullptr)
        return;

    Window rootWindow = DefaultRootWindow(display);
    if (rootWindow == None || !callback(localDisplay, rootWindow, userParameter))
        return;

    RunEnumX11Windows(localDisplay, rootWindow, callback, userParameter);
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
        {
            INGAMEOVERLAY_DEBUG("Key {} to native {}", (int)item.lib_key, item.native_key);
            return item.native_key;
        }
    }

    return 0;
}

static bool XlibBuildNativeKeyCombination(InGameOverlay::ToggleKey toggleKeys[], int toggleKeysCount, std::vector<uint32_t>& nativeKeyCombination)
{
    for (int i = 0; i < toggleKeysCount; ++i)
    {
        uint32_t k = ToggleKeyToNativeKey(toggleKeys[i]);
        if (k != 0 && std::find(nativeKeyCombination.begin(), nativeKeyCombination.end(), k) == nativeKeyCombination.end())
            nativeKeyCombination.emplace_back(k);
    }

    return true;
}

static bool XCBBuildNativeKeyCombination(
    xcb_connection_t* xcbConnection,
    InGameOverlay::ToggleKey toggleKeys[],
    int toggleKeysCount,
    std::vector<uint32_t> &nativeKeyCombination)
{
    if (xcbConnection == nullptr ||
        toggleKeys == nullptr ||
        toggleKeysCount <= 0)
    {
        return false;
    }

    const xcb_setup_t* setup = xcb_get_setup(xcbConnection);
    if (setup == nullptr)
        return false;

    const int minKeycode = setup->min_keycode;
    const int maxKeycode = setup->max_keycode;

    xcb_get_keyboard_mapping_cookie_t cookie =
        xcb_get_keyboard_mapping(
            xcbConnection,
            minKeycode,
            maxKeycode - minKeycode + 1);

    xcb_generic_error_t* error = nullptr;

    xcb_get_keyboard_mapping_reply_t* reply =
        xcb_get_keyboard_mapping_reply(
            xcbConnection,
            cookie,
            &error);

    if (error != nullptr)
    {
        free(error);
        return false;
    }

    if (reply == nullptr)
        return false;

    const int keysymsPerKeycode =
        reply->keysyms_per_keycode;

    const xcb_keysym_t* keysyms =
        xcb_get_keyboard_mapping_keysyms(reply);

    nativeKeyCombination.clear();
    nativeKeyCombination.reserve(toggleKeysCount);

    for (int i = 0; i < toggleKeysCount; ++i)
    {
        const auto keySym =
            ToggleKeyToNativeKey(toggleKeys[i]);

        if (keySym == NoSymbol)
        {
            free(reply);
            nativeKeyCombination.clear();
            return false;
        }

        bool found = false;

        for (int keycode = minKeycode;
            keycode <= maxKeycode;
            ++keycode)
        {
            const int index =
                (keycode - minKeycode) * keysymsPerKeycode;

            for (int column = 0;
                column < keysymsPerKeycode;
                ++column)
            {
                if (keysyms[index + column] == keySym)
                {
                    if (keycode != 0 && std::find(nativeKeyCombination.begin(), nativeKeyCombination.end(), static_cast<uint32_t>(keycode)) == nativeKeyCombination.end())
                        nativeKeyCombination.emplace_back(static_cast<uint32_t>(keycode));

                    INGAMEOVERLAY_DEBUG(
                        "Toggle key {} -> KeySym {} -> KeyCode {}",
                        static_cast<int>(toggleKeys[i]),
                        static_cast<uint32_t>(keySym),
                        keycode);

                    found = true;
                    break;
                }
            }

            if (found)
                break;
        }

        if (!found)
        {
            INGAMEOVERLAY_ERROR(
                "Failed to resolve ToggleKey {} to XCB keycode.",
                static_cast<int>(toggleKeys[i]));

            free(reply);
            nativeKeyCombination.clear();
            return false;
        }
    }

    free(reply);

    return nativeKeyCombination.size() ==
        static_cast<size_t>(toggleKeysCount);
}

static inline bool GetKeyState(Display* d, KeySym keySym, char szKey[32])
{
    int iKeyCodeToFind = XKeysymToKeycode(d, keySym);

    return szKey[iKeyCodeToFind / 8] & (1 << (iKeyCodeToFind % 8));
}

bool X11Hook_t::_XlibLoadHook()
{
    void* hX11 = System::Library::GetLibraryHandle(X11_DLL_NAME);
    if (hX11 == nullptr)
    {
        INGAMEOVERLAY_ERROR("Failed to hook Xlib: Cannot find {}", X11_DLL_NAME);
        return false;
    }

    struct {
        void** func_ptr;
        const char* func_name;
    } hook_array[] = {
        { (void**)&_XEventsQueued    , "XEventsQueued" },
        { (void**)&_XPending         , "XPending"      },
        { (void**)&_XQueryPointer    , "XQueryPointer" },
    };

    for (auto& entry : hook_array)
    {
        *entry.func_ptr = System::Library::GetSymbol(hX11, entry.func_name);
        if (entry.func_ptr == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to load Xlib: Event function {} missing.", entry.func_name);
            return false;
        }
    }

    INGAMEOVERLAY_INFO("Loaded Xlib hook.");

    return true;
}

bool X11Hook_t::_XlibStartHook()
{
    struct {
        void** func_ptr;
        void* hook_ptr;
        const char* func_name;
    } hook_array[] = {
        { (void**)&_XEventsQueued    , (void*)&X11Hook_t::MyXEventsQueued, "XEventsQueued" },
        { (void**)&_XPending         , (void*)&X11Hook_t::MyXPending     , "XPending"      },
        { (void**)&_XQueryPointer    , (void*)&X11Hook_t::MyXQueryPointer, "XQueryPointer" },
    };

    BeginHook();

    for (auto& entry : hook_array)
    {
        HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr));
    }

    EndHook();

    _X11HookMode = X11HookMode_t::HookXlib;
    _Hooked = true;

    void* hX11 = System::Library::GetLibraryHandle(X11_DLL_NAME);
    LibraryName = System::Library::GetLibraryPath(hX11);

    INGAMEOVERLAY_INFO("Started Xlib hook.");

    return true;
}

void X11Hook_t::_XlibResetRenderState(OverlayHookState state)
{
    if (!_Initialized)
        return;

    _PressedKeycodes.clear();
    _GameWnd = 0;

    HideAppInputs(false);
    HideOverlayInputs(true);

    ImGui_ImplX11_Shutdown();
    _Initialized = false;
}

bool X11Hook_t::_XlibSetInitialWindowSize(Display* display, Window wnd)
{
    unsigned int width, height;
    Window unused_window;
    int unused_int;
    unsigned int unused_unsigned_int;

    XGetGeometry(display, wnd, &unused_window, &unused_int, &unused_int, &width, &height, &unused_unsigned_int, &unused_unsigned_int);

    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
    return true;
}

bool X11Hook_t::_XlibPrepareForOverlay(Display* display, Window wnd)
{
    if (_GameWnd != wnd)
        _XlibResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        if (!ImGui_ImplX11_Init(display, (unsigned int)wnd, (void*)_XQueryPointer))
            return false;

        //XSelectInput(display,
        //    wnd,
        //    SubstructureRedirectMask | SubstructureNotifyMask |
        //    KeyPressMask | KeyReleaseMask |
        //    ButtonPressMask | ButtonReleaseMask |
        //    FocusChangeMask | ExposureMask);

        _GameWnd = wnd;
        _Initialized = true;
    }

    if (!_OverlayInputsHidden)
    {
        if (!ImGui_ImplX11_NewFrame())
            return false;
    }

    return true;
}

bool X11Hook_t::_XCBLoadHook()
{
    void* hXCB = System::Library::GetLibraryHandle(XCB_DLL_NAME);
    if (hXCB == nullptr)
    {
        INGAMEOVERLAY_ERROR("Failed to hook XCB: Cannot find {}", XCB_DLL_NAME);
        return false;
    }

    struct {
        void** func_ptr;
        const char* func_name;
        bool mandatory;
    } hook_array[] = {
        { (void**)&_XCBPollForEvent       , "xcb_poll_for_event"       , true  },
        { (void**)&_XCBQueryPointerReply  , "xcb_query_pointer_reply"  , false },
        { (void**)&_XCBQueryExtension     , "xcb_query_extension"      , false },
        { (void**)&_XCBQueryExtensionReply, "xcb_query_extension_reply", false },
    };

    for (auto& entry : hook_array)
    {
        *entry.func_ptr = System::Library::GetSymbol(hXCB, entry.func_name);
        if (entry.func_ptr == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to load XCB: Event function {} missing.", entry.func_name);
            return false;
        }
    }

    INGAMEOVERLAY_INFO("Loaded XCB hook.");

    return true;
}

bool X11Hook_t::_XCBStartHook()
{
    void* hXCB = System::Library::GetLibraryHandle(XCB_DLL_NAME);
    LibraryName = System::Library::GetLibraryPath(hXCB);

    struct {
        void** func_ptr;
        void* hook_ptr;
        const char* func_name;
    } hook_array[] = {
        { (void**)&_XCBPollForEvent     , (void*)&X11Hook_t::MyXCBPollForEvent, "xcb_poll_for_event" },
        { (void**)&_XCBQueryPointerReply, (void*)&X11Hook_t::MyXCBQueryPointerReply, "xcb_query_pointer_reply" },
    };

    BeginHook();
    
    for (auto& entry : hook_array)
    {
        if (*entry.func_ptr)
        {
            if (!HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr)))
            {
                INGAMEOVERLAY_INFO("Failed to hook {}", entry.func_name);
            }
        }
    }
    
    EndHook();

    _X11HookMode = X11HookMode_t::HookXCB;
    _Hooked = true;

    INGAMEOVERLAY_INFO("Started XCB hook.");

    return true;
}

void X11Hook_t::_XCBResetRenderState(OverlayHookState state)
{
    if (!_Initialized)
        return;

    _PressedKeycodes.clear();
    _GameWnd = XCB_WINDOW_NONE;
    _XCBXInputOpcode = 0;

    HideAppInputs(false);
    HideOverlayInputs(true);

    ImGui_ImplXCB_Shutdown();
    _Initialized = false;
}

bool X11Hook_t::_XCBSetInitialWindowSize(xcb_connection_t* xcbConnection, xcb_window_t wnd)
{
    if (xcbConnection == nullptr || wnd == XCB_WINDOW_NONE)
        return false;

    auto cookie = xcb_get_geometry(xcbConnection, wnd);

    xcb_generic_error_t* error = nullptr;
    auto* reply = xcb_get_geometry_reply(xcbConnection, cookie, &error);

    if (reply == nullptr)
    {
        if (error != nullptr)
            free(error);

        return false;
    }

    ImGui::GetIO().DisplaySize = ImVec2((float)reply->width, (float)reply->height);

    free(reply);

    if (error != nullptr)
        free(error);

    return true;
}

bool X11Hook_t::_XCBPrepareForOverlay(xcb_connection_t* xcbConnection, xcb_window_t wnd)
{
    if (xcbConnection == nullptr)
        return false;

    if (_GameWnd != wnd)
        _XCBResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        if (!ImGui_ImplXCB_Init((void*)xcbConnection, (unsigned int)wnd, (void*)_XCBQueryPointerReply))
            return false;

        _XCBQueryXInputExtension(xcbConnection);

        _GameWnd = wnd;
        _Initialized = true;
    }

    if (!_OverlayInputsHidden)
    {
        if (!ImGui_ImplXCB_NewFrame())
            return false;
    }

    return true;
}

xcb_connection_t* X11Hook_t::_GetXCBConnection(Display* display)
{
    if (_XGetXCBConnection != nullptr)
        return _XGetXCBConnection(display);

    return nullptr;
}

void X11Hook_t::_XCBQueryXInputExtension(xcb_connection_t* xcbConnection)
{
    if (_XCBQueryExtension == nullptr || _XCBQueryExtensionReply == nullptr)
        return;

    const char* name = "XInputExtension";

    auto cookie = _XCBQueryExtension(
        xcbConnection,
        strlen(name),
        name
    );

    auto* reply = _XCBQueryExtensionReply(
        xcbConnection,
        cookie,
        nullptr
    );

    if (reply && reply->present)
    {
        uint8_t majorOpcode = reply->major_opcode;
        uint8_t firstEvent = reply->first_event;
        uint8_t firstError = reply->first_error;

        INGAMEOVERLAY_DEBUG(
            "XInputExtension present: opcode={} event={} error={}",
            majorOpcode,
            firstEvent,
            firstError
        );

        _XCBXInputOpcode = reply->major_opcode;
    }

    free(reply);
}

bool X11Hook_t::_IsKeyCombinationPressed() const
{
    if (_PressedKeycodes.size() != _NativeKeyCombination.size())
        return false;

    return std::equal(
        _PressedKeycodes.begin(),
        _PressedKeycodes.end(),
        _NativeKeyCombination.begin());
}

bool X11Hook_t::StartHook(std::function<void()>& keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount)
{
    if (!_Hooked)
    {
        if (!keyCombinationCallback)
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: No key combination callback.");
            return false;
        }

        if (toggleKeys == nullptr || toggleKeysCount <= 0)
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: No key combination.");
            return false;
        }

        void* hX11 = System::Library::GetLibraryHandle(X11_DLL_NAME);
        if (hX11 == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to hook X11: Cannot find {}", X11_DLL_NAME);
            return false;
        }

        _XGetXCBConnection = (decltype(_XGetXCBConnection))System::Library::GetSymbol(hX11, "XGetXCBConnection");
        if (_XGetXCBConnection == nullptr)
        {
            INGAMEOVERLAY_INFO("XGetXCBConnection not found in {}", X11_DLL_NAME);

            void* hX11xcb = System::Library::GetLibraryHandle(X11_XCB_DLL_NAME);
            if (hX11xcb != nullptr)
            {
                _XGetXCBConnection = (decltype(_XGetXCBConnection))System::Library::GetSymbol(hX11xcb, "XGetXCBConnection");
                if (_XGetXCBConnection == nullptr)
                {
                    INGAMEOVERLAY_INFO("XGetXCBConnection not found in {}", X11_XCB_DLL_NAME);
                }
            }
        }

        if (_XGetXCBConnection != nullptr)
        {
            // Loading the XCB hook failed, fallback to Xlib
            if (!_XCBLoadHook())
                _XGetXCBConnection = nullptr;
        }

        // Early hook Xlib because we cannot get the XCB connection anyway
        if (_XGetXCBConnection == nullptr)
        {
            if (!_XlibLoadHook())
                return false;

            if (!_XlibStartHook())
                return false;
        }
        else
        {
            _XlibLoadHook();
        }

        _KeyCombinationCallback = std::move(keyCombinationCallback);
        _OverlayToggleKeys.assign(toggleKeys, toggleKeys + toggleKeysCount);
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
    switch (_X11HookMode)
    {
        case X11HookMode_t::HookXlib: _XlibResetRenderState(state); break;
        case X11HookMode_t::HookXCB : _XCBResetRenderState(state); break;
        default                     : break;
    }
}

bool X11Hook_t::SetInitialWindowSize(Display* display, Window wnd)
{
    if (!_Hooked)
    {
        auto* xcbConnection = _GetXCBConnection(display);

        if (xcbConnection != nullptr)
        {
            if (!_XCBStartHook())
                return false;
        }
        else
        {
            if (!_XlibStartHook())
                return false;
        }
    }

    switch (_X11HookMode)
    {
        case X11HookMode_t::HookXlib:
        {
            XlibBuildNativeKeyCombination(_OverlayToggleKeys.data(), _OverlayToggleKeys.size(), _NativeKeyCombination);
            return _XlibSetInitialWindowSize(display, wnd);
        }

        case X11HookMode_t::HookXCB:
        {
            auto* xcbConnection = _XGetXCBConnection(display);
            if (xcbConnection == nullptr)
                return false;

            XCBBuildNativeKeyCombination(xcbConnection, _OverlayToggleKeys.data(), _OverlayToggleKeys.size(), _NativeKeyCombination);
            return _XCBSetInitialWindowSize(xcbConnection, (xcb_window_t)wnd);
        }

        default: break;
    }

    return false;
}

bool X11Hook_t::PrepareForOverlay(void* display_, uint32_t wnd)
{
    auto* display = (Display*)display_;

    if(!_Hooked)
        return false;

    switch (_X11HookMode)
    {
        case X11HookMode_t::HookXlib: return _XlibPrepareForOverlay(display, wnd);
        case X11HookMode_t::HookXCB : return _XCBPrepareForOverlay(_XGetXCBConnection(display), (xcb_window_t)wnd);
        default                     : break;
    }

    return false;
}

std::vector<X11Hook_t::X11WindowEnumerationResult_t> X11Hook_t::FindApplicationX11Window(int32_t processId)
{
    struct
    {
        int32_t pid;
        std::vector<X11WindowEnumerationResult_t> windows;
        Atom pidAtom;
    } windowParams{
        processId,
        {},
        None
    };

    EnumX11Windows([](std::shared_ptr<SafeXlibDisplay_t> display, Window window, void* userParameter) -> int
    {
        auto params = reinterpret_cast<decltype(windowParams)*>(userParameter);
        if (params->pidAtom == None)
            params->pidAtom = XInternAtom(static_cast<Display*>(display->DisplayHandle), "_NET_WM_PID", True);

        if (params->pidAtom == None)
            return 0;

        XTextProperty data;
        int status = XGetTextProperty(static_cast<Display*>(display->DisplayHandle), window, &data, params->pidAtom);
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

        INGAMEOVERLAY_TRACE("Display: {}, Window: {}", (void*)display->DisplayHandle, (uint32_t)window);

        if (processId == params->pid)
            params->windows.emplace_back(display, window);

        return 1;
    }, &windowParams);

    return windowParams.windows;
}

/////////////////////////////////////////////////////////////////////////////////////
// X11 window hooks
static bool XlibIgnoreEvent(XEvent &event)
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

        default: break;
    }
    return false;
}

static bool XCBIgnoreExtensionEvent(const xcb_generic_event_t* event, uint8_t xinputOpcode)
{
    auto* ge = reinterpret_cast<const xcb_ge_generic_event_t*>(event);

    if (xinputOpcode != 0 && ge->extension == xinputOpcode)
    {
        switch (ge->event_type)
        {
            case XCB_INPUT_MOTION:
            case XCB_INPUT_RAW_MOTION:
            case XCB_INPUT_BUTTON_PRESS:
            case XCB_INPUT_BUTTON_RELEASE:
                return true;
        }
    }

    return false;
}

static bool XCBIgnoreEvent(const xcb_generic_event_t* event, uint8_t xinputOpcode)
{
    const uint8_t type = event->response_type & 0x7f;

    switch (type)
    {
        case XCB_KEY_PRESS:
        case XCB_KEY_RELEASE:

        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE:

        case XCB_MOTION_NOTIFY:

        case XCB_SELECTION_REQUEST:
            return true;

        case XCB_GE_GENERIC:
            return XCBIgnoreExtensionEvent(event, xinputOpcode);

        default: break;
    }
    return false;
}

int X11Hook_t::_XlibCheckForOverlay(Display *d, int num_events)
{
    char szKey[32];

    if( _Initialized )
    {
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
                            Window tmpSavedRoot;
                            Window tmpSavedChild;

                            _XQueryPointer(d, _GameWnd, &tmpSavedRoot, &tmpSavedChild, &_SavedCursorRX, &_SavedCursorRY, &_SavedCursorX, &_SavedCursorY, &_SavedMask);

                            _SavedRoot = static_cast<uint32_t>(tmpSavedRoot);
                            _SavedChild = static_cast<uint32_t>(tmpSavedChild);
                        }

                        _KeyCombinationPushed = true;
                    }
                }
                else
                {
                    _KeyCombinationPushed = false;
                }
            }

            if (event.type == FocusIn || event.type == FocusOut)
            {
                ImGui::GetIO().SetAppAcceptingEvents(event.type == FocusIn);
            }

            if (!hide_overlay_inputs || event.type == FocusIn || event.type == FocusOut)
            {
                ImGui_ImplX11_EventHandler(event, pNextEvent);
            }

            if (!hide_app_inputs || !XlibIgnoreEvent(event))
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

X11Hook_t::XCBEventDecision_t X11Hook_t::_XCBCheckForOverlay(
    xcb_connection_t* xcbConnection,
    xcb_generic_event_t* event,
    xcb_generic_event_t* nextEvent,
    bool isNextEvent)
{
    (void)xcbConnection;

    const uint8_t type = event->response_type & 0x7f;

    bool hide_app_inputs = _ApplicationInputsHidden;

    bool hide_overlay_inputs = _OverlayInputsHidden;

    if (type == XCB_KEY_PRESS)
    {
        auto* keyEvent = reinterpret_cast<xcb_key_press_event_t*>(event);

        const auto keycode = keyEvent->detail;

        INGAMEOVERLAY_DEBUG("Key pressed: {} time={}", keycode, keyEvent->time);

        if (keyEvent->time != _XCBLastKeyReleaseTime)
        {
            if (std::find(
                _PressedKeycodes.begin(),
                _PressedKeycodes.end(),
                keycode) == _PressedKeycodes.end())
            {
                _PressedKeycodes.emplace_back(keycode);
            }
        }
    }
    else if (type == XCB_KEY_RELEASE)
    {
        auto* keyEvent = reinterpret_cast<xcb_key_release_event_t*>(event);

        const auto keycode = keyEvent->detail;

        INGAMEOVERLAY_DEBUG("Key released: {} time={}", keycode, keyEvent->time);

        if (!isNextEvent)
        {
            _XCBLastKeyReleaseTime = keyEvent->time;

            return {
                _ApplicationInputsHidden && XCBIgnoreEvent(event, _XCBXInputOpcode), // consume
                true, // needsNextEvent
            };
        }

        /*
         * We are now in the second pass
         * of KEY_RELEASE.
         *
         * nextEvent is the event that actually follows
         * the release.
         */
        auto isAutoRepeat = false;

        if (nextEvent && (nextEvent->response_type & 0x7f) == XCB_KEY_PRESS)
        {
            auto* nextKeyEvent = reinterpret_cast<xcb_key_press_event_t*>(nextEvent);

            isAutoRepeat =
                nextKeyEvent->detail == keyEvent->detail &&
                nextKeyEvent->time == keyEvent->time;
        }

        if (!isAutoRepeat)
        {
            auto it = std::find(_PressedKeycodes.begin(), _PressedKeycodes.end(), keycode);

            if (it != _PressedKeycodes.end())
            {
                _PressedKeycodes.erase(it);
            }
        }
    }
    else
    {
        switch (type)
        {
            case XCB_KEY_PRESS: INGAMEOVERLAY_TRACE("XCB_KEY_PRESS"); break;
            case XCB_KEY_RELEASE: INGAMEOVERLAY_TRACE("XCB_KEY_RELEASE"); break;
            case XCB_BUTTON_PRESS: INGAMEOVERLAY_TRACE("XCB_BUTTON_PRESS"); break;
            case XCB_BUTTON_RELEASE: INGAMEOVERLAY_TRACE("XCB_BUTTON_RELEASE"); break;
            case XCB_MOTION_NOTIFY: INGAMEOVERLAY_TRACE("XCB_MOTION_NOTIFY"); break;
            case XCB_ENTER_NOTIFY: INGAMEOVERLAY_TRACE("XCB_ENTER_NOTIFY"); break;
            case XCB_LEAVE_NOTIFY: INGAMEOVERLAY_TRACE("XCB_LEAVE_NOTIFY"); break;
            case XCB_FOCUS_IN: INGAMEOVERLAY_TRACE("XCB_FOCUS_IN"); break;
            case XCB_FOCUS_OUT: INGAMEOVERLAY_TRACE("XCB_FOCUS_OUT"); break;
            case XCB_KEYMAP_NOTIFY: INGAMEOVERLAY_TRACE("XCB_KEYMAP_NOTIFY"); break;
            case XCB_EXPOSE: INGAMEOVERLAY_TRACE("XCB_EXPOSE"); break;
            case XCB_GRAPHICS_EXPOSURE: INGAMEOVERLAY_TRACE("XCB_GRAPHICS_EXPOSURE"); break;
            case XCB_NO_EXPOSURE: INGAMEOVERLAY_TRACE("XCB_NO_EXPOSURE"); break;
            case XCB_VISIBILITY_NOTIFY: INGAMEOVERLAY_TRACE("XCB_VISIBILITY_NOTIFY"); break;
            case XCB_CREATE_NOTIFY: INGAMEOVERLAY_TRACE("XCB_CREATE_NOTIFY"); break;
            case XCB_DESTROY_NOTIFY: INGAMEOVERLAY_TRACE("XCB_DESTROY_NOTIFY"); break;
            case XCB_UNMAP_NOTIFY: INGAMEOVERLAY_TRACE("XCB_UNMAP_NOTIFY"); break;
            case XCB_MAP_NOTIFY: INGAMEOVERLAY_TRACE("XCB_MAP_NOTIFY"); break;
            case XCB_MAP_REQUEST: INGAMEOVERLAY_TRACE("XCB_MAP_REQUEST"); break;
            case XCB_REPARENT_NOTIFY: INGAMEOVERLAY_TRACE("XCB_REPARENT_NOTIFY"); break;
            case XCB_CONFIGURE_NOTIFY: INGAMEOVERLAY_TRACE("XCB_CONFIGURE_NOTIFY"); break;
            case XCB_CONFIGURE_REQUEST: INGAMEOVERLAY_TRACE("XCB_CONFIGURE_REQUEST"); break;
            case XCB_GRAVITY_NOTIFY: INGAMEOVERLAY_TRACE("XCB_GRAVITY_NOTIFY"); break;
            case XCB_RESIZE_REQUEST: INGAMEOVERLAY_TRACE("XCB_RESIZE_REQUEST"); break;
            case XCB_CIRCULATE_NOTIFY: INGAMEOVERLAY_TRACE("XCB_CIRCULATE_NOTIFY"); break;
            case XCB_CIRCULATE_REQUEST: INGAMEOVERLAY_TRACE("XCB_CIRCULATE_REQUEST"); break;
            case XCB_PROPERTY_NOTIFY: INGAMEOVERLAY_TRACE("XCB_PROPERTY_NOTIFY"); break;
            case XCB_SELECTION_CLEAR: INGAMEOVERLAY_TRACE("XCB_SELECTION_CLEAR"); break;
            case XCB_SELECTION_REQUEST: INGAMEOVERLAY_TRACE("XCB_SELECTION_REQUEST"); break;
            case XCB_SELECTION_NOTIFY: INGAMEOVERLAY_TRACE("XCB_SELECTION_NOTIFY"); break;
            case XCB_COLORMAP_NOTIFY: INGAMEOVERLAY_TRACE("XCB_COLORMAP_NOTIFY"); break;
            case XCB_CLIENT_MESSAGE: INGAMEOVERLAY_TRACE("XCB_CLIENT_MESSAGE"); break;
            case XCB_MAPPING_NOTIFY: INGAMEOVERLAY_TRACE("XCB_MAPPING_NOTIFY"); break;
            case XCB_GE_GENERIC: INGAMEOVERLAY_TRACE("XCB_GE_GENERIC"); break;
        }
    }

    if (type == XCB_KEY_PRESS ||
        type == XCB_KEY_RELEASE)
    {
        const auto combinationPressed = _IsKeyCombinationPressed();

        if (combinationPressed)
        {
            if (!_KeyCombinationPushed)
            {
                INGAMEOVERLAY_DEBUG("Key combination pressed");

                _KeyCombinationCallback();

                if (_OverlayInputsHidden)
                    hide_overlay_inputs = true;

                if (_ApplicationInputsHidden)
                {
                    hide_app_inputs = true;
                    _HasSavedCursor = false;
                }

                _KeyCombinationPushed = true;
            }
        }
        else
        {
            _KeyCombinationPushed = false;
        }
    }

    if (!hide_overlay_inputs)
    {
        ImGui_ImplXCB_EventHandler(event, nextEvent);
    }

    return {
        hide_app_inputs && XCBIgnoreEvent(event, _XCBXInputOpcode), // consume
        false, // needsNextEvent
    };
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

    int res = inst->_XEventsQueued(display, mode);

    if( res )
    {
        res = inst->_XlibCheckForOverlay(display, res);
    }

    return res;
}

int X11Hook_t::MyXPending(Display* display)
{
    X11Hook_t* inst = X11Hook_t::Inst();

    int res = inst->_XPending(display);

    if( res )
    {
        res = inst->_XlibCheckForOverlay(display, res);
    }

    return res;
}

xcb_generic_event_t* X11Hook_t::MyXCBPollForEvent(
    xcb_connection_t* connection)
{
    auto* inst = X11Hook_t::Inst();

    if (!inst->_Initialized)
    {
        if (inst->_XCBPendingEvent.event)
        {
            auto* event =
                inst->_XCBPendingEvent.event;

            inst->_XCBPendingEvent.event = nullptr;
            inst->_XCBPendingEvent.decision = {};

            return event;
        }

        return inst->_XCBPollForEvent(connection);
    }

    while (true)
    {
        xcb_generic_event_t* event = nullptr;
        XCBEventDecision_t decision;

        /*
         * Event already analyzed previously.
         */
        if (inst->_XCBPendingEvent.event)
        {
            event = inst->_XCBPendingEvent.event;

            decision = inst->_XCBPendingEvent.decision;

            inst->_XCBPendingEvent.event = nullptr;
            inst->_XCBPendingEvent.decision = {};
        }
        else
        {
            event = inst->_XCBPollForEvent(connection);

            if (!event)
                return nullptr;

            decision = inst->_XCBCheckForOverlay(connection, event, nullptr, false);
        }

        /*
         * Normal processing.
         */
        if (!decision.needsNextEvent)
        {
            if (decision.consume)
            {
                free(event);
                continue;
            }

            return event;
        }

        /*
         * The handler requests exactly one
         * additional event.
         */
        auto* nextEvent = inst->_XCBPollForEvent(connection);

        /*
         * Pass the first event back to the handler with:
         *
         *   nextEvent   = the following event, or nullptr
         *   isNextEvent = true
         *
         * The handler can therefore finalize its KEY_RELEASE.
         */
        decision = inst->_XCBCheckForOverlay(connection,event, nextEvent, true);

        /*
         * The first event has now been fully processed.
         */
        if (decision.consume)
        {
            free(event);
        }
        else
        {
            /*
             * It must be returned before nextEvent.
             *
             * Analyze nextEvent now and preserve its decision
             * so that it is never processed twice.
             */
            if (nextEvent)
            {
                auto nextDecision = inst->_XCBCheckForOverlay(connection, nextEvent, nullptr, false);

                inst->_XCBPendingEvent.event = nextEvent;

                inst->_XCBPendingEvent.decision = nextDecision;
            }

            return event;
        }

        /*
         * The first event has been consumed.
         *
         * nextEvent now becomes the next candidate.
         */
        if (!nextEvent)
            return nullptr;

        auto nextDecision = inst->_XCBCheckForOverlay(connection, nextEvent, nullptr, false);

        if (nextDecision.consume)
        {
            free(nextEvent);
            continue;
        }

        /*
         * It has already been processed, so we can
         * return it directly.
         */
        return nextEvent;
    }
}

xcb_query_pointer_reply_t* X11Hook_t::MyXCBQueryPointerReply(xcb_connection_t* connection, xcb_query_pointer_cookie_t cookie, xcb_generic_error_t** error)
{
    X11Hook_t* inst = X11Hook_t::Inst();

    auto* reply = inst->_XCBQueryPointerReply(
        connection,
        cookie,
        error
    );

    if (reply &&
        inst->_Initialized &&
        inst->_ApplicationInputsHidden)
    {
        if (!inst->_HasSavedCursor)
        {
            inst->_HasSavedCursor = true;
            inst->_SavedRoot = reply->root;
            inst->_SavedChild = reply->child;
            inst->_SavedCursorRX = reply->root_x;
            inst->_SavedCursorRY = reply->root_y;
            inst->_SavedCursorX = reply->win_x;
            inst->_SavedCursorY = reply->win_y;
            inst->_SavedMask = reply->mask;
        }
        else
        {
            reply->root = inst->_SavedRoot;
            reply->child = inst->_SavedChild;
            reply->root_x = inst->_SavedCursorRX;
            reply->root_y = inst->_SavedCursorRY;
            reply->win_x = inst->_SavedCursorX;
            reply->win_y = inst->_SavedCursorY;
            reply->mask = inst->_SavedMask;
        }
    }

    return reply;
}

/////////////////////////////////////////////////////////////////////////////////////

X11Hook_t::X11Hook_t()
    : _Initialized(false)
    , _X11HookMode(X11HookMode_t::HookNone)
    , _Hooked(false)
    , _GameWnd(0)
    , _HasSavedCursor(false)
    , _SavedRoot(0)
    , _SavedChild(0)
    , _XGetXCBConnection(nullptr)
    , _XCBLastKeyReleaseTime(0)
    , _XCBXInputOpcode(0)
    , _XCBQueryExtension(nullptr)
    , _XCBQueryExtensionReply(nullptr)
    , _KeyCombinationPushed(false)
    , _ApplicationInputsHidden(false)
    , _OverlayInputsHidden(true)
    , _XQueryPointer(nullptr)
    , _XEventsQueued(nullptr)
    , _XPending(nullptr)
    , _XCBPollForEvent(nullptr)
    , _XCBQueryPointerReply(nullptr)
{
}

X11Hook_t::~X11Hook_t()
{
    INGAMEOVERLAY_INFO("X11 Hook removed");

    ResetRenderState(OverlayHookState::Removing);

    _inst->UnhookAll();
    _inst = nullptr;
}

X11Hook_t* X11Hook_t::Inst()
{
    if (_inst == nullptr)
        _inst = new X11Hook_t;

    return _inst;
}

const char* X11Hook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

}// namespace InGameOverlay