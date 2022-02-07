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

bool Windows_Hook::start_hook(std::function<bool(bool)>& _key_combination_callback)
{
    if (!hooked)
    {
        void* hUser32 = System::Library::GetModuleHandle(DLL_NAME);
        if (hUser32 == nullptr)
        {
            SPDLOG_WARN("Failed to hook Windows: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library libUser32;
        LibraryName = System::Library::GetModulePath(hUser32);
        if (!libUser32.OpenLibrary(LibraryName, false))
        {
            SPDLOG_WARN("Failed to hook Windows: Cannot load {}", LibraryName);
            return false;
        }

        GetRawInputBuffer = libUser32.GetSymbol<decltype(::GetRawInputBuffer)>("GetRawInputBuffer");
        GetRawInputData   = libUser32.GetSymbol<decltype(::GetRawInputData)>("GetRawInputData");
        GetKeyState       = libUser32.GetSymbol<decltype(::GetKeyState)>("GetKeyState");
        GetAsyncKeyState  = libUser32.GetSymbol<decltype(::GetAsyncKeyState)>("GetAsyncKeyState");
        GetKeyboardState  = libUser32.GetSymbol<decltype(::GetKeyboardState)>("GetKeyboardState");
        GetCursorPos      = libUser32.GetSymbol<decltype(::GetCursorPos)>("GetCursorPos");
        SetCursorPos      = libUser32.GetSymbol<decltype(::SetCursorPos)>("SetCursorPos");

        if(GetRawInputBuffer == nullptr ||
           GetRawInputData   == nullptr ||
           GetKeyState       == nullptr ||
           GetAsyncKeyState  == nullptr ||
           GetKeyboardState  == nullptr ||
           GetCursorPos      == nullptr ||
           SetCursorPos      == nullptr)
        {
            SPDLOG_ERROR("Failed to hook Windows: Events functions missing.");
            return false;
        }

        SPDLOG_INFO("Hooked Windows");
        key_combination_callback = std::move(_key_combination_callback);

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)GetRawInputBuffer, &Windows_Hook::MyGetRawInputBuffer),
            std::make_pair<void**, void*>(&(PVOID&)GetRawInputData  , &Windows_Hook::MyGetRawInputData),
            std::make_pair<void**, void*>(&(PVOID&)GetKeyState      , &Windows_Hook::MyGetKeyState),
            std::make_pair<void**, void*>(&(PVOID&)GetAsyncKeyState , &Windows_Hook::MyGetAsyncKeyState),
            std::make_pair<void**, void*>(&(PVOID&)GetKeyboardState , &Windows_Hook::MyGetKeyboardState),
            std::make_pair<void**, void*>(&(PVOID&)GetCursorPos     , &Windows_Hook::MyGetCursorPos),
            std::make_pair<void**, void*>(&(PVOID&)SetCursorPos     , &Windows_Hook::MySetCursorPos)
        );
        EndHook();

        hooked = true;
    }
    return true;
}

void Windows_Hook::resetRenderState()
{
    if (initialized)
    {
        initialized = false;
        SetWindowLongPtr(_game_hwnd, GWLP_WNDPROC, (LONG_PTR)_game_wndproc);
        _game_hwnd = nullptr;
        _game_wndproc = nullptr;
        ImGui_ImplWin32_Shutdown();
    }
}

bool Windows_Hook::prepareForOverlay(HWND hWnd)
{
    if (_game_hwnd != hWnd)
        resetRenderState();

    if (!initialized)
    {
        _game_hwnd = hWnd;
        ImGui_ImplWin32_Init(_game_hwnd);

        _game_wndproc = (WNDPROC)SetWindowLongPtr(_game_hwnd, GWLP_WNDPROC, (LONG_PTR)&Windows_Hook::HookWndProc);
        initialized = true;
    }

    if (initialized)
    {
        void* current_proc = (void*)GetWindowLongPtr(_game_hwnd, GWLP_WNDPROC);
        if (current_proc == nullptr)
            return false;

        ImGui_ImplWin32_NewFrame();
        // Read keyboard modifiers inputs
        auto& io = ImGui::GetIO();

        POINT pos;
        if (this->GetCursorPos(&pos) && ScreenToClient(hWnd, &pos))
        {
            io.MousePos = ImVec2((float)pos.x, (float)pos.y);
        }

        io.KeyCtrl  = (this->GetKeyState(VK_CONTROL) & 0x8000) != 0;
        io.KeyShift = (this->GetKeyState(VK_SHIFT) & 0x8000) != 0;
        io.KeyAlt   = (this->GetKeyState(VK_MENU) & 0x8000) != 0;
        return true;
    }

    return false;
}

HWND Windows_Hook::GetGameHwnd() const
{
    return _game_hwnd;
}

WNDPROC Windows_Hook::GetGameWndProc() const
{
    return _game_wndproc;
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
        case WM_INPUT:
            return true;
    }
    return false;
}

void RawEvent(RAWINPUT& raw)
{
    HWND hWnd = Windows_Hook::Inst()->GetGameHwnd();
    switch(raw.header.dwType)
    {
        case RIM_TYPEMOUSE:
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_LBUTTONDOWN, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_LBUTTONUP, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_RBUTTONDOWN, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_RBUTTONUP, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_MBUTTONDOWN, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_MBUTTONUP, 0, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_MOUSEWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);
            if (raw.data.mouse.usButtonFlags & RI_MOUSE_HWHEEL)
                ImGui_ImplWin32_WndProcHandler(hWnd, WM_MOUSEHWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);
            break;

        //case RIM_TYPEKEYBOARD:
            //ImGui_ImplWin32_WndProcHandler(hWnd, raw.data.keyboard.Message, raw.data.keyboard.VKey, 0);
            //break;
    }
}

LRESULT CALLBACK Windows_Hook::HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    bool skip_input = inst->key_combination_callback(false);
    bool clean_keys = false;
    if (inst->initialized)
    {
        // Is the event is a key press
        if (uMsg == WM_KEYDOWN)
        {
            // Tab is pressed and was not pressed before
            if (wParam == VK_TAB && !(lParam & (1 << 30)))
            {
                // If Left Shift is pressed
                if (inst->GetAsyncKeyState(VK_LSHIFT) & (1 << 15))
                {
                    if (inst->key_combination_callback(true))
                    {
                        skip_input = true;
                        // Save the last known cursor pos when opening the overlay
                        // so we can spoof the GetCursorPos return value.
                        inst->GetCursorPos(&inst->_saved_cursor_pos);
                    }
                    else
                    {
                        clean_keys = true;
                    }
                }
            }
        }

        if (skip_input && IgnoreMsg(uMsg))
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            if (clean_keys)
            {
                auto& io = ImGui::GetIO();
                memset(io.KeysDown, 0, sizeof(io.KeysDown));
            }
            return 0;
        }
    }

    // Protect against recursive call of the WindowProc...
    if (inst->in_proc)
        return 0;

    inst->in_proc = true;
    // Call the overlay window procedure
    auto res = CallWindowProc(Windows_Hook::Inst()->_game_wndproc, hWnd, uMsg, wParam, lParam);
    inst->in_proc = false;
    return res;
}

UINT WINAPI Windows_Hook::MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    int res = inst->GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    if (!inst->initialized)
        return res;

    if (pData != nullptr)
    {
        for (int i = 0; i < res; ++i)
            RawEvent(pData[i]);
    }

    if (!inst->key_combination_callback(false))
        return res;

    return 0;
}

UINT WINAPI Windows_Hook::MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    auto res = inst->GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (!inst->initialized || pData == nullptr)
        return res;

    if (uiCommand == RID_INPUT && res == sizeof(RAWINPUT))
        RawEvent(*reinterpret_cast<RAWINPUT*>(pData));

    if (!inst->key_combination_callback(false))
        return res;

    memset(pData, 0, *pcbSize);
    *pcbSize = 0;
    return 0;
}

SHORT WINAPI Windows_Hook::MyGetKeyState(int nVirtKey)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->initialized && inst->key_combination_callback(false))
        return 0;

    return inst->GetKeyState(nVirtKey);
}

SHORT WINAPI Windows_Hook::MyGetAsyncKeyState(int vKey)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->initialized && inst->key_combination_callback(false))
        return 0;

    return inst->GetAsyncKeyState(vKey);
}

BOOL WINAPI Windows_Hook::MyGetKeyboardState(PBYTE lpKeyState)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->initialized && inst->key_combination_callback(false))
        return FALSE;

    return inst->GetKeyboardState(lpKeyState);
}

BOOL  WINAPI Windows_Hook::MyGetCursorPos(LPPOINT lpPoint)
{
    Windows_Hook* inst = Windows_Hook::Inst();
    
    BOOL res = inst->GetCursorPos(lpPoint);
    if (inst->initialized && inst->key_combination_callback(false) && lpPoint != nullptr)
    {
        *lpPoint = inst->_saved_cursor_pos;
    }

    return res;
}

BOOL WINAPI Windows_Hook::MySetCursorPos(int X, int Y)
{
    Windows_Hook* inst = Windows_Hook::Inst();

    if (inst->initialized && inst->key_combination_callback(false))
    {// That way, it will fail only if the real API fails.
     // Hides error messages on some Unity debug builds.
        POINT pos;
        inst->GetCursorPos(&pos);
        X = pos.x;
        Y = pos.y;
    }

    return inst->SetCursorPos(X, Y);
}

/////////////////////////////////////////////////////////////////////////////////////

Windows_Hook::Windows_Hook() :
    initialized(false),
    hooked(false),
    in_proc(false),
    _game_hwnd(nullptr),
    _game_wndproc(nullptr),
    GetRawInputBuffer(nullptr),
    GetRawInputData(nullptr),
    GetKeyState(nullptr),
    GetAsyncKeyState(nullptr),
    GetKeyboardState(nullptr)
{
}

Windows_Hook::~Windows_Hook()
{
    SPDLOG_INFO("Windows Hook removed");

    resetRenderState();

    _inst = nullptr;
}

Windows_Hook* Windows_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new Windows_Hook;

    return _inst;
}

std::string Windows_Hook::GetLibraryName() const
{
    return LibraryName;
}