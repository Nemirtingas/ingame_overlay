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

#include <ingame_overlay/Renderer_Hook.h>

#include "../internal_includes.h"

class Windows_Hook :
    public Base_Hook
{
public:
    static constexpr const char* DLL_NAME = "user32.dll";

private:
    static Windows_Hook* _inst;

    // Variables
    bool hooked;
    bool initialized;
    bool in_proc;
    HWND _game_hwnd;
    WNDPROC _game_wndproc;
    POINT _saved_cursor_pos;

    // Functions
    Windows_Hook();

    // Hook to Windows window messages
    decltype(::GetRawInputBuffer) *GetRawInputBuffer;
    decltype(::GetRawInputData)   *GetRawInputData;
    decltype(::GetKeyState)       *GetKeyState;
    decltype(::GetAsyncKeyState)  *GetAsyncKeyState;
    decltype(::GetKeyboardState)  *GetKeyboardState;
    decltype(::GetCursorPos)      *GetCursorPos;
    decltype(::SetCursorPos)      *SetCursorPos;

    static LRESULT CALLBACK HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static UINT  WINAPI MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);
    static UINT  WINAPI MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
    static SHORT WINAPI MyGetKeyState(int nVirtKey);
    static SHORT WINAPI MyGetAsyncKeyState(int vKey);
    static BOOL  WINAPI MyGetKeyboardState(PBYTE lpKeyState);
    static BOOL  WINAPI MyGetCursorPos(LPPOINT lpPoint);
    static BOOL  WINAPI MySetCursorPos(int X, int Y);

    // In (bool): Is toggle wanted
    // Out(bool): Is the overlay visible, if true, inputs will be disabled
    std::function<bool(bool)> key_combination_callback;

public:
    std::string LibraryName;

    virtual ~Windows_Hook();

    void resetRenderState();
    bool prepareForOverlay(HWND hWnd);

    HWND GetGameHwnd() const;
    WNDPROC GetGameWndProc() const;

    bool start_hook(std::function<bool(bool)>& key_combination_callbacl);
    static Windows_Hook* Inst();
    virtual std::string GetLibraryName() const;
};
