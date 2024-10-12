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

#include "WindowsHook.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <System/Library.h>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace InGameOverlay {

constexpr decltype(WindowsHook_t::DLL_NAME) WindowsHook_t::DLL_NAME;

WindowsHook_t* WindowsHook_t::_inst = nullptr;

static int ToggleKeyToNativeKey(InGameOverlay::ToggleKey k)
{
    struct {
        InGameOverlay::ToggleKey lib_key;
        int native_key;
    } mapping[] = {
        { InGameOverlay::ToggleKey::ALT  , VK_MENU    },
        { InGameOverlay::ToggleKey::CTRL , VK_CONTROL },
        { InGameOverlay::ToggleKey::SHIFT, VK_SHIFT   },
        { InGameOverlay::ToggleKey::TAB  , VK_TAB     },
        { InGameOverlay::ToggleKey::F1   , VK_F1      },
        { InGameOverlay::ToggleKey::F2   , VK_F2      },
        { InGameOverlay::ToggleKey::F3   , VK_F3      },
        { InGameOverlay::ToggleKey::F4   , VK_F4      },
        { InGameOverlay::ToggleKey::F5   , VK_F5      },
        { InGameOverlay::ToggleKey::F6   , VK_F6      },
        { InGameOverlay::ToggleKey::F7   , VK_F7      },
        { InGameOverlay::ToggleKey::F8   , VK_F8      },
        { InGameOverlay::ToggleKey::F9   , VK_F9      },
        { InGameOverlay::ToggleKey::F10  , VK_F10     },
        { InGameOverlay::ToggleKey::F11  , VK_F11     },
        { InGameOverlay::ToggleKey::F12  , VK_F12     },
    };

    for (auto const& item : mapping)
    {
        if (item.lib_key == k)
            return item.native_key;
    }

    return 0;
}

bool WindowsHook_t::StartHook(std::function<void()>& _key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys)
{
    if (!_Hooked)
    {
        if (!_key_combination_callback)
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination callback.");
            return false;
        }

        if (toggle_keys.empty())
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination.");
            return false;
        }

        void* hUser32 = System::Library::GetLibraryHandle(DLL_NAME);
        if (hUser32 == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library::Library libUser32;
        LibraryName = System::Library::GetLibraryPath(hUser32);
        if (!libUser32.OpenLibrary(LibraryName, false))
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot load {}", LibraryName);
            return false;
        }

        struct {
            void** func_ptr;
            void* hook_ptr;
            const char* func_name;
        } hook_array[] = {
            { (void**)&_TranslateMessage , nullptr                                    , "TranslateMessage"  },
            { (void**)&_DefWindowProcA   , nullptr                                    , "DefWindowProcA"    },
            { (void**)&_DefWindowProcW   , nullptr                                    , "DefWindowProcW"    },
            { (void**)&_GetRawInputBuffer, (void*)&WindowsHook_t::_MyGetRawInputBuffer, "GetRawInputBuffer" },
            { (void**)&_GetRawInputData  , (void*)&WindowsHook_t::_MyGetRawInputData  , "GetRawInputData"   },
            { (void**)&_GetKeyState      , (void*)&WindowsHook_t::_MyGetKeyState      , "GetKeyState"       },
            { (void**)&_GetAsyncKeyState , (void*)&WindowsHook_t::_MyGetAsyncKeyState , "GetAsyncKeyState"  },
            { (void**)&_GetKeyboardState , (void*)&WindowsHook_t::_MyGetKeyboardState , "GetKeyboardState"  },
            { (void**)&_GetCursorPos     , (void*)&WindowsHook_t::_MyGetCursorPos     , "GetCursorPos"      },
            { (void**)&_SetCursorPos     , (void*)&WindowsHook_t::_MySetCursorPos     , "SetCursorPos"      },
            { (void**)&_GetClipCursor    , (void*)&WindowsHook_t::_MyGetClipCursor    , "GetClipCursor"     },
            { (void**)&_ClipCursor       , (void*)&WindowsHook_t::_MyClipCursor       , "ClipCursor"        },
            { (void**)&_GetMessageA      , (void*)&WindowsHook_t::_MyGetMessageA      , "GetMessageA"       },
            { (void**)&_GetMessageW      , (void*)&WindowsHook_t::_MyGetMessageW      , "GetMessageW"       },
            { (void**)&_PeekMessageA     , (void*)&WindowsHook_t::_MyPeekMessageA     , "PeekMessageA"      },
            { (void**)&_PeekMessageW     , (void*)&WindowsHook_t::_MyPeekMessageW     , "PeekMessageW"      },
        };

        for (auto& entry : hook_array)
        {
            *entry.func_ptr = libUser32.GetSymbol<void*>(entry.func_name);
            if (entry.func_ptr == nullptr)
            {
                INGAMEOVERLAY_ERROR("Failed to hook Windows: failed to load function {}.", entry.func_name);
                return false;
            }
        }

        INGAMEOVERLAY_INFO("Hooked Windows");
        _KeyCombinationCallback = std::move(_key_combination_callback);

        for (auto& key : toggle_keys)
        {
            uint32_t k = ToggleKeyToNativeKey(key);
            if (k != 0)
            {
                _NativeKeyCombination.insert(k);
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

        _Hooked = true;
    }
    return true;
}

void WindowsHook_t::HideAppInputs(bool hide)
{
    _ApplicationInputsHidden = hide;
    if (hide)
    {
        _ClipCursor(&_DefaultClipCursor);
    }
    else
    {
        _ClipCursor(&_SavedClipCursor);
    }
}

void WindowsHook_t::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void WindowsHook_t::ResetRenderState(OverlayHookState state)
{
    if (!_Initialized)
        return;

    _Initialized = false;

    HideAppInputs(false);
    HideOverlayInputs(true);

    ImGui_ImplWin32_Shutdown();
}

void WindowsHook_t::SetInitialWindowSize(HWND hWnd)
{
    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(hWnd, &rect);
    ImGui::GetIO().DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
}

short WindowsHook_t::_ImGuiGetKeyState(int nVirtKey)
{
    return WindowsHook_t::Inst()->_GetKeyState(nVirtKey);
}

bool WindowsHook_t::PrepareForOverlay(HWND hWnd)
{
    if (_GameHwnd != hWnd)
        ResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        _GameHwnd = hWnd;
        ImGui_ImplWin32_Init(_GameHwnd, &WindowsHook_t::_ImGuiGetKeyState);

        _Initialized = true;
    }

    if (_Initialized)
    {
        if (!_OverlayInputsHidden)
        {
            ImGui_ImplWin32_NewFrame();
        }

        return true;
    }

    return false;
}

std::vector<HWND> WindowsHook_t::FindApplicationHWND(DWORD processId)
{
    struct
    {
        DWORD pid;
        std::vector<HWND> windows;
    } windowParams{
        processId
    };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        if (!IsWindowVisible(hwnd) && !IsIconic(hwnd))
            return TRUE;

        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);

        auto params = reinterpret_cast<decltype(windowParams)*>(lParam);

        if (processId == params->pid)
            params->windows.emplace_back(hwnd);

        return TRUE;
    }, reinterpret_cast<LPARAM>(&windowParams));

    return windowParams.windows;
}

/////////////////////////////////////////////////////////////////////////////////////
// Windows window hooks
static bool IgnoreMsg(UINT uMsg)
{
    switch (uMsg)
    {
        // Mouse Events
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_LBUTTONUP: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_MOUSEACTIVATE: case WM_MOUSEHOVER: case WM_MOUSELEAVE:
        // Keyboard Events
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSDEADCHAR:
        case WM_CHAR: case WM_UNICHAR: case WM_DEADCHAR:
        // Raw Input Events
        //case WM_INPUT: // Don't ignore, we will handle it and ignore in GetRawInputBuffer/GetRawInputData
            return true;
    }
    return false;
}

void WindowsHook_t::_RawEvent(RAWINPUT& raw)
{
    switch (raw.header.dwType)
    {
    case RIM_TYPEMOUSE:
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_LBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_LBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_RBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_RBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_MBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_MBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_MOUSEWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_HWHEEL)
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_MOUSEHWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);

        if (raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0)
        {
            POINT p;
            _GetCursorPos(&p);
            ::ScreenToClient(_GameHwnd, &p);
            ImGui_ImplWin32_WndProcHandler(_GameHwnd, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        }
        break;

        //case RIM_TYPEKEYBOARD:
            //ImGui_ImplWin32_WndProcHandler(_GameHwnd, raw.data.keyboard.Message, raw.data.keyboard.VKey, 0);
            //break;
    }
}

bool WindowsHook_t::_HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    bool hide_app_inputs = _ApplicationInputsHidden;
    bool hide_overlay_inputs = _OverlayInputsHidden;
    
    if (_Initialized)
    {
        // Is the event is a key press
        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
        {
            int key_count = 0;
            for (auto const& key : _NativeKeyCombination)
            {
                if (_GetAsyncKeyState(key) & (1 << 15))
                    ++key_count;
            }
    
            if (key_count == _NativeKeyCombination.size())
            {// All shortcut keys are pressed
                if (!_KeyCombinationPushed)
                {
                    _KeyCombinationCallback();
    
                    if (_OverlayInputsHidden)
                        hide_overlay_inputs = true;
    
                    if(_ApplicationInputsHidden)
                    {
                        hide_app_inputs = true;
    
                        // Save the last known cursor pos when opening the overlay
                        // so we can spoof the GetCursorPos return value.
                        _GetCursorPos(&_SavedCursorPos);
                        _GetClipCursor(&_SavedClipCursor);
                    }
                    else
                    {
                        _ClipCursor(&_SavedClipCursor);
                    }
                    _KeyCombinationPushed = true;
                }
            }
            else
            {
                _KeyCombinationPushed = false;
            }
        }
    
        if (uMsg == WM_KILLFOCUS || uMsg == WM_SETFOCUS)
        {
            ImGui::GetIO().SetAppAcceptingEvents(uMsg == WM_SETFOCUS);
        }
    
        if (!hide_overlay_inputs || uMsg == WM_KILLFOCUS || uMsg == WM_SETFOCUS)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        }
    
        if (hide_app_inputs && IgnoreMsg(uMsg))
            return true;
    }
    
    return false;
}

UINT WINAPI WindowsHook_t::_MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    int res = inst->_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized)
        return res;

    if (!inst->_OverlayInputsHidden && pData != nullptr)
    {
        for (int i = 0; i < res; ++i)
            inst->_RawEvent(pData[i]);
    }

    if (!inst->_ApplicationInputsHidden)
        return res;

    return 0;
}

UINT WINAPI WindowsHook_t::_MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    auto res = inst->_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized || pData == nullptr)
        return res;

    if (!inst->_OverlayInputsHidden && uiCommand == RID_INPUT && res == sizeof(RAWINPUT))
        inst->_RawEvent(*reinterpret_cast<RAWINPUT*>(pData));

    if (!inst->_ApplicationInputsHidden)
        return res;

    memset(pData, 0, *pcbSize);
    *pcbSize = 0;
    return 0;
}

SHORT WINAPI WindowsHook_t::_MyGetKeyState(int nVirtKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetKeyState(nVirtKey);
}

SHORT WINAPI WindowsHook_t::_MyGetAsyncKeyState(int vKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetAsyncKeyState(vKey);
}

BOOL WINAPI WindowsHook_t::_MyGetKeyboardState(PBYTE lpKeyState)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return FALSE;

    return inst->_GetKeyboardState(lpKeyState);
}

BOOL  WINAPI WindowsHook_t::_MyGetCursorPos(LPPOINT lpPoint)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    BOOL res = inst->_GetCursorPos(lpPoint);
    if (inst->_Initialized && inst->_ApplicationInputsHidden && lpPoint != nullptr)
    {
        *lpPoint = inst->_SavedCursorPos;
    }

    return res;
}

BOOL WINAPI WindowsHook_t::_MySetCursorPos(int X, int Y)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_SetCursorPos(X, Y);

    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyGetClipCursor(RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    if (lpRect == nullptr || !inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_GetClipCursor(lpRect);

    *lpRect = inst->_SavedClipCursor;
    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyClipCursor(CONST RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    CONST RECT* v = lpRect == nullptr ? &inst->_DefaultClipCursor : lpRect;

    inst->_SavedClipCursor = *v;

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_ClipCursor(v);
    
    return inst->_ClipCursor(&inst->_DefaultClipCursor);
}

BOOL WINAPI WindowsHook_t::_MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageA(lpMsg);
    inst->_DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageW(lpMsg);
    inst->_DefWindowProcW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE) && inst->_ApplicationInputsHidden && IgnoreMsg(lpMsg->message))
    {
        // Remove message from queue
        inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE | (wRemoveMsg & (~PM_REMOVE)));
    }

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageA(lpMsg);
    inst->_DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE) && inst->_ApplicationInputsHidden && IgnoreMsg(lpMsg->message))
    {
        // Remove message from queue
        inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE | (wRemoveMsg & (~PM_REMOVE)));
    }

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageW(lpMsg);
    inst->_DefWindowProcW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

/////////////////////////////////////////////////////////////////////////////////////

WindowsHook_t::WindowsHook_t() :
    _Hooked(false),
    _Initialized(false),
    _GameHwnd(nullptr),
    _SavedCursorPos{},
    _SavedClipCursor{},
    _DefaultClipCursor{ LONG(0xFFFF8000), LONG(0xFFFF8000), LONG(0x00007FFF), LONG(0x00007FFF) },
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _KeyCombinationPushed(false),
    _TranslateMessage(nullptr),
    _DefWindowProcA(nullptr),
    _DefWindowProcW(nullptr),
    _GetRawInputBuffer(nullptr),
    _GetRawInputData(nullptr),
    _GetKeyState(nullptr),
    _GetAsyncKeyState(nullptr),
    _GetKeyboardState(nullptr),
    _GetCursorPos(nullptr),
    _SetCursorPos(nullptr),
    _GetClipCursor(nullptr),
    _ClipCursor(nullptr),
    _GetMessageA(nullptr),
    _GetMessageW(nullptr),
    _PeekMessageA(nullptr),
    _PeekMessageW(nullptr)
{
}

WindowsHook_t::~WindowsHook_t()
{
    INGAMEOVERLAY_INFO("Windows Hook removed");

    ResetRenderState(OverlayHookState::Removing);

    _inst = nullptr;
}

WindowsHook_t* WindowsHook_t::Inst()
{
    if (_inst == nullptr)
        _inst = new WindowsHook_t;

    return _inst;
}

const char* WindowsHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

}//namespace InGameOverlay