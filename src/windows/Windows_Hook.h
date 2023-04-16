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
    bool _Hooked;
    bool _Initialized;
    int _RecurseCallCount;
    HWND _GameHwnd;
    POINT _SavedCursorPos;
    RECT _SavedClipCursor;
    CONST RECT _DefaultClipCursor;
    bool _ApplicationInputsHidden;
    bool _OverlayInputsHidden;

    // In (bool): Is toggle wanted
    // Out(bool): Is the overlay visible, if true, inputs will be disabled
    std::function<void()> _KeyCombinationCallback;
    std::set<int> _NativeKeyCombination;
    bool _KeyCombinationPushed;

    // Functions
    Windows_Hook();
    void _RawEvent(RAWINPUT& raw);
    bool _HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    decltype(::TranslateMessage)* _TranslateMessage;
    decltype(::DefWindowProcA)  * _DefWindowProcA;
    decltype(::DefWindowProcW)  * _DefWindowProcW;

    // Hook to Windows window messages
    decltype(::GetRawInputBuffer) *_GetRawInputBuffer;
    decltype(::GetRawInputData)   *_GetRawInputData;
    decltype(::GetKeyState)       *_GetKeyState;
    decltype(::GetAsyncKeyState)  *_GetAsyncKeyState;
    decltype(::GetKeyboardState)  *_GetKeyboardState;
    decltype(::GetCursorPos)      *_GetCursorPos;
    decltype(::SetCursorPos)      *_SetCursorPos;
    decltype(::GetClipCursor)     *_GetClipCursor;
    decltype(::ClipCursor)        *_ClipCursor;
    decltype(::GetMessageA)       *_GetMessageA;
    decltype(::GetMessageW)       *_GetMessageW;
    decltype(::PeekMessageA)      *_PeekMessageA;
    decltype(::PeekMessageW)      *_PeekMessageW;

    static UINT  WINAPI MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);
    static UINT  WINAPI MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
    static SHORT WINAPI MyGetKeyState(int nVirtKey);
    static SHORT WINAPI MyGetAsyncKeyState(int vKey);
    static BOOL  WINAPI MyGetKeyboardState(PBYTE lpKeyState);
    static BOOL  WINAPI MyGetCursorPos(LPPOINT lpPoint);
    static BOOL  WINAPI MySetCursorPos(int X, int Y);
    static BOOL  WINAPI MyGetClipCursor(RECT* lpRect);
    static BOOL  WINAPI MyClipCursor(CONST RECT* lpRect);
    static BOOL  WINAPI MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    static BOOL  WINAPI MyGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    static BOOL  WINAPI MyPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
    static BOOL  WINAPI MyPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

public:
    std::string LibraryName;

    virtual ~Windows_Hook();

    void ResetRenderState();
    void SetInitialWindowSize(HWND hWnd);
    bool PrepareForOverlay(HWND hWnd);

    bool StartHook(std::function<void()>& key_combination_callback, std::set<ingame_overlay::ToggleKey> const& toggle_keys);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static Windows_Hook* Inst();
    virtual std::string GetLibraryName() const;
};
