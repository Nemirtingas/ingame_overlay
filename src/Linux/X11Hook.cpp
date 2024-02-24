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

#include "X11Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_x11.h>
#include <System/Library.h>

extern int ImGui_ImplX11_EventHandler(XEvent& event, XEvent* nextEvent);

namespace InGameOverlay {

constexpr decltype(X11_Hook::DLL_NAME) X11_Hook::DLL_NAME;

X11_Hook* X11_Hook::_inst = nullptr;

uint32_t ToggleKeyToNativeKey(InGameOverlay::ToggleKey k)
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

bool GetKeyState(Display* d, KeySym keySym, char szKey[32])
{
    int iKeyCodeToFind = XKeysymToKeycode(d, keySym);

    return szKey[iKeyCodeToFind / 8] & (1 << (iKeyCodeToFind % 8));
}

bool X11_Hook::StartHook(std::function<void()>& _key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys)
{
    if (!_Hooked)
    {
        if (!_key_combination_callback)
        {
            SPDLOG_ERROR("Failed to hook X11: No key combination callback.");
            return false;
        }

        if (toggle_keys.empty())
        {
            SPDLOG_ERROR("Failed to hook X11: No key combination.");
            return false;
        }

        void* hX11 = System::Library::GetLibraryHandle(DLL_NAME);
        if (hX11 == nullptr)
        {
            SPDLOG_WARN("Failed to hook X11: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library::Library libX11;
        LibraryName = System::Library::GetLibraryPath(hX11);

        if (!libX11.OpenLibrary(LibraryName, false))
        {
            SPDLOG_WARN("Failed to hook X11: Cannot load {}", LibraryName);
            return false;
        }

        struct {
            void** func_ptr;
            void* hook_ptr;
            const char* func_name;
        } hook_array[] = {
            { (void**)&_XEventsQueued, (void*)&X11_Hook::MyXEventsQueued, "XEventsQueued" },
            { (void**)&_XPending     , (void*)&X11_Hook::MyXPending     , "XPending"      },
            { (void**)&_XQueryPointer, (void*)&X11_Hook::MyXQueryPointer, "XQueryPointer" },
        };

        for (auto& entry : hook_array)
        {
            *entry.func_ptr = libX11.GetSymbol<void*>(entry.func_name);
            if (entry.func_ptr == nullptr)
            {
                SPDLOG_ERROR("Failed to hook X11: Event function {} missing.", entry.func_name);
                return false;
            }
        }

        SPDLOG_INFO("Hooked X11");

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

        BeginHook();
        
        for (auto& entry : hook_array)
        {
            HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr));
        }

        EndHook();
    }
    return true;
}

void X11_Hook::HideAppInputs(bool hide)
{
    _ApplicationInputsHidden = hide;
}

void X11_Hook::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void X11_Hook::ResetRenderState()
{
    if (_Initialized)
    {
        _GameWnd = 0;
        _Initialized = false;

        HideAppInputs(false);
        HideOverlayInputs(true);

        ImGui_ImplX11_Shutdown();
    }
}

void X11_Hook::SetInitialWindowSize(Display* display, Window wnd)
{
    unsigned int width, height;
    Window unused_window;
    int unused_int;
    unsigned int unused_unsigned_int;

    XGetGeometry(display, wnd, &unused_window, &unused_int, &unused_int, &width, &height, &unused_unsigned_int, &unused_unsigned_int);

    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
}

bool X11_Hook::PrepareForOverlay(Display *display, Window wnd)
{
    if(!_Hooked)
        return false;

    if (_GameWnd != wnd)
        ResetRenderState();

    if (!_Initialized)
    {
        ImGui_ImplX11_Init(display, (void*)wnd, (void*)_XQueryPointer);
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

int X11_Hook::_CheckForOverlay(Display *d, int num_events)
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

            if (event.type == FocusIn || event.type == FocusOut)
            {
                ImGui::GetIO().SetAppAcceptingEvents(event.type == FocusIn);
            }

            if (!hide_overlay_inputs || event.type == FocusIn || event.type == FocusOut)
            {
                ImGui_ImplX11_EventHandler(event, pNextEvent);
            }

            if (!hide_app_inputs || !IgnoreEvent(event))
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

Bool X11_Hook::MyXQueryPointer(Display* display, Window w, Window* root_return, Window* child_return, int* root_x_return, int* root_y_return, int* win_x_return, int* win_y_return, unsigned int* mask_return)
{
    X11_Hook* inst = X11_Hook::Inst();

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

int X11_Hook::MyXEventsQueued(Display *display, int mode)
{
    X11_Hook* inst = X11_Hook::Inst();

    int res = inst->_XEventsQueued(display, mode);

    if( res )
    {
        res = inst->_CheckForOverlay(display, res);
    }

    return res;
}

int X11_Hook::MyXPending(Display* display)
{
    int res = Inst()->_XPending(display);

    if( res )
    {
        res = Inst()->_CheckForOverlay(display, res);
    }

    return res;
}

/////////////////////////////////////////////////////////////////////////////////////

X11_Hook::X11_Hook() :
    _Initialized(false),
    _Hooked(false),
    _GameWnd(0),
    _KeyCombinationPushed(false),
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _XQueryPointer(nullptr),
    _XEventsQueued(nullptr),
    _XPending(nullptr)
{
}

X11_Hook::~X11_Hook()
{
    SPDLOG_INFO("X11 Hook removed");

    ResetRenderState();

    _inst = nullptr;
}

X11_Hook* X11_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new X11_Hook;

    return _inst;
}

std::string X11_Hook::GetLibraryName() const
{
    return LibraryName;
}

}// namespace InGameOverlay