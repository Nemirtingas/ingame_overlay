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

#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <System/Library.h>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

constexpr decltype(Windows_Hook::DLL_NAME) Windows_Hook::DLL_NAME;

Windows_Hook* Windows_Hook::_inst = nullptr;

static int ToggleKeyToNativeKey(ingame_overlay::ToggleKey k)
{
    struct {
        ingame_overlay::ToggleKey lib_key;
        int native_key;
    } mapping[] = {
        { ingame_overlay::ToggleKey::ALT  , VK_MENU    },
        { ingame_overlay::ToggleKey::CTRL , VK_CONTROL },
        { ingame_overlay::ToggleKey::SHIFT, VK_SHIFT   },
        { ingame_overlay::ToggleKey::TAB  , VK_TAB     },
        { ingame_overlay::ToggleKey::F1   , VK_F1      },
        { ingame_overlay::ToggleKey::F2   , VK_F2      },
        { ingame_overlay::ToggleKey::F3   , VK_F3      },
        { ingame_overlay::ToggleKey::F4   , VK_F4      },
        { ingame_overlay::ToggleKey::F5   , VK_F5      },
        { ingame_overlay::ToggleKey::F6   , VK_F6      },
        { ingame_overlay::ToggleKey::F7   , VK_F7      },
        { ingame_overlay::ToggleKey::F8   , VK_F8      },
        { ingame_overlay::ToggleKey::F9   , VK_F9      },
        { ingame_overlay::ToggleKey::F10  , VK_F10     },
        { ingame_overlay::ToggleKey::F11  , VK_F11     },
        { ingame_overlay::ToggleKey::F12  , VK_F12     },
    };

    for (auto const& item : mapping)
    {
        if (item.lib_key == k)
            return item.native_key;
    }

    return 0;
}

bool Windows_Hook::StartHook(std::function<void()>& _key_combination_callback, std::set<ingame_overlay::ToggleKey> const& toggle_keys)
{
    if (!_Hooked)
    {
        if (!_key_combination_callback)
        {
            SPDLOG_ERROR("Failed to hook Windows: No key combination callback.");
            return false;
        }

        if (toggle_keys.empty())
        {
            SPDLOG_ERROR("Failed to hook Windows: No key combination.");
            return false;
        }

        void* hUser32 = System::Library::GetLibraryHandle(DLL_NAME);
        if (hUser32 == nullptr)
        {
            SPDLOG_WARN("Failed to hook Windows: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library::Library libUser32;
        LibraryName = System::Library::GetLibraryPath(hUser32);
        if (!libUser32.OpenLibrary(LibraryName, false))
        {
            SPDLOG_WARN("Failed to hook Windows: Cannot load {}", LibraryName);
            return false;
        }

        struct {
            void** func_ptr;
            void* hook_ptr;
            const char* func_name;
        } hook_array[] = {
            { (void**)&_TranslateMessage , nullptr                                  , "TranslateMessage"  },
            { (void**)&_DefWindowProcA   , nullptr                                  , "DefWindowProcA"    },
            { (void**)&_DefWindowProcW   , nullptr                                  , "DefWindowProcW"    },
            { (void**)&_GetRawInputBuffer, (void*)&Windows_Hook::MyGetRawInputBuffer, "GetRawInputBuffer" },
            { (void**)&_GetRawInputData  , (void*)&Windows_Hook::MyGetRawInputData  , "GetRawInputData"   },
            { (void**)&_GetKeyState      , (void*)&Windows_Hook::MyGetKeyState      , "GetKeyState"       },
            { (void**)&_GetAsyncKeyState , (void*)&Windows_Hook::MyGetAsyncKeyState , "GetAsyncKeyState"  },
            { (void**)&_GetKeyboardState , (void*)&Windows_Hook::MyGetKeyboardState , "GetKeyboardState"  },
            { (void**)&_GetCursorPos     , (void*)&Windows_Hook::MyGetCursorPos     , "GetCursorPos"      },
            { (void**)&_SetCursorPos     , (void*)&Windows_Hook::MySetCursorPos     , "SetCursorPos"      },
            { (void**)&_GetClipCursor    , (void*)&Windows_Hook::MyGetClipCursor    , "GetClipCursor"     },
            { (void**)&_ClipCursor       , (void*)&Windows_Hook::MyClipCursor       , "ClipCursor"        },
            { (void**)&_GetMessageA      , (void*)&Windows_Hook::MyGetMessageA      , "GetMessageA"       },
            { (void**)&_GetMessageW      , (void*)&Windows_Hook::MyGetMessageW      , "GetMessageW"       },
            { (void**)&_PeekMessageA     , (void*)&Windows_Hook::MyPeekMessageA     , "PeekMessageA"      },
            { (void**)&_PeekMessageW     , (void*)&Windows_Hook::MyPeekMessageW     , "PeekMessageW"      },
        };

        for (auto& entry : hook_array)
        {
            *entry.func_ptr = libUser32.GetSymbol<void*>(entry.func_name);
            if (entry.func_ptr == nullptr)
            {
                SPDLOG_ERROR("Failed to hook Windows: failed to load function {}.", entry.func_name);
                return false;
            }
        }

        SPDLOG_INFO("Hooked Windows");
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
                HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr));
        }

        EndHook();

        _Hooked = true;
    }
    return true;
}

void Windows_Hook::HideAppInputs(bool hide)
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

void Windows_Hook::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void Windows_Hook::ResetRenderState()
{
    if (_Initialized)
    {
        _Initialized = false;

        HideAppInputs(false);
        HideOverlayInputs(true);

        ImGui_ImplWin32_Shutdown();
    }
}

void Windows_Hook::SetInitialWindowSize(HWND hWnd)
{
    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(hWnd, &rect);
    ImGui::GetIO().DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
}

short Windows_Hook::ImGuiGetKeyState(int nVirtKey)
{
    return Windows_Hook::Inst()->_GetKeyState(nVirtKey);
}

bool Windows_Hook::PrepareForOverlay(HWND hWnd)
{
    if (_GameHwnd != hWnd)
        ResetRenderState();

    if (!_Initialized)
    {
        _GameHwnd = hWnd;
        ImGui_ImplWin32_Init(_GameHwnd, &Windows_Hook::ImGuiGetKeyState);

        _Initialized = true;
    }

    if (_Initialized)
    {
        if (!_OverlayInputsHidden)
        {
            ImGui_ImplWin32_NewFrame();
            // Read keyboard modifiers inputs
            //auto& io = ImGui::GetIO();
            //
            //POINT pos;
            //if (this->_GetCursorPos(&pos) && ScreenToClient(hWnd, &pos))
            //{
            //    io.AddMousePosEvent((float)pos.x, (float)pos.y);
            //}
        }

        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////
// Windows window hooks
bool IgnoreMsg(UINT uMsg)
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

void Windows_Hook::_RawEvent(RAWINPUT& raw)
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

bool Windows_Hook::_HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
    
    // Protect against recursive call of the WindowProc...
    if (_RecurseCallCount > 16)
        return true;
    
    //++inst->_RecurseCallCount;
    // Call the overlay window procedure
    //auto res = CallWindowProc(Windows_Hook::Inst()->_GameWndProc, hWnd, uMsg, wParam, lParam);
    //--inst->_RecurseCallCount;
    return false;
}

UINT WINAPI Windows_Hook::MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    int res = inst->_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized)
        return res;

    if (!inst->_OverlayInputsHidden)
    {
        if (pData != nullptr)
        {
            for (int i = 0; i < res; ++i)
                inst->_RawEvent(pData[i]);
        }
    }

    if (!inst->_ApplicationInputsHidden)
        return res;

    return 0;
}

UINT WINAPI Windows_Hook::MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    auto res = inst->_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized || pData == nullptr)
        return res;

    if (uiCommand == RID_INPUT && res == sizeof(RAWINPUT))
        inst->_RawEvent(*reinterpret_cast<RAWINPUT*>(pData));

    if (!inst->_ApplicationInputsHidden)
        return res;

    memset(pData, 0, *pcbSize);
    *pcbSize = 0;
    return 0;
}

SHORT WINAPI Windows_Hook::MyGetKeyState(int nVirtKey)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetKeyState(nVirtKey);
}

SHORT WINAPI Windows_Hook::MyGetAsyncKeyState(int vKey)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetAsyncKeyState(vKey);
}

BOOL WINAPI Windows_Hook::MyGetKeyboardState(PBYTE lpKeyState)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return FALSE;

    return inst->_GetKeyboardState(lpKeyState);
}

BOOL  WINAPI Windows_Hook::MyGetCursorPos(LPPOINT lpPoint)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    
    BOOL res = inst->_GetCursorPos(lpPoint);
    if (inst->_Initialized && inst->_ApplicationInputsHidden && lpPoint != nullptr)
    {
        *lpPoint = inst->_SavedCursorPos;
    }

    return res;
}

BOOL WINAPI Windows_Hook::MySetCursorPos(int X, int Y)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_SetCursorPos(X, Y);

    return TRUE;
}

BOOL WINAPI Windows_Hook::MyGetClipCursor(RECT* lpRect)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    if (lpRect == nullptr || !inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_GetClipCursor(lpRect);

    *lpRect = inst->_SavedClipCursor;
    return TRUE;
}

BOOL WINAPI Windows_Hook::MyClipCursor(CONST RECT* lpRect)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    CONST RECT* v = lpRect == nullptr ? &inst->_DefaultClipCursor : lpRect;

    inst->_SavedClipCursor = *v;

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_ClipCursor(v);
    
    return inst->_ClipCursor(&inst->_DefaultClipCursor);
}

BOOL WINAPI Windows_Hook::MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    Windows_Hook* inst = Windows_Hook::Inst();
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

BOOL WINAPI Windows_Hook::MyGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    Windows_Hook* inst = Windows_Hook::Inst();
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

BOOL WINAPI Windows_Hook::MyPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    Windows_Hook* inst = Windows_Hook::Inst();
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

BOOL WINAPI Windows_Hook::MyPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    Windows_Hook* inst = Windows_Hook::Inst();
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

Windows_Hook::Windows_Hook() :
    _Initialized(false),
    _Hooked(false),
    _GameHwnd(nullptr),
    _RecurseCallCount(0),
    _DefaultClipCursor{ LONG(0xFFFF8000), LONG(0xFFFF8000), LONG(0x00007FFF), LONG(0x00007FFF) },
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _KeyCombinationPushed(false)
{
}

Windows_Hook::~Windows_Hook()
{
    SPDLOG_INFO("Windows Hook removed");

    ResetRenderState();

    _inst = nullptr;
}

Windows_Hook* Windows_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new Windows_Hook;

    return _inst;
}

const std::string& Windows_Hook::GetLibraryName() const
{
    return LibraryName;
}