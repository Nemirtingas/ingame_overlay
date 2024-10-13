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

namespace InGameOverlay {

class WindowsHook_t :
    public BaseHook_t
{
public:
    static constexpr const char* DLL_NAME = "user32.dll";

private:
    static WindowsHook_t* _inst;

    // Variables
    bool _Hooked;
    bool _Initialized;
    HWND _GameHwnd;
    POINT _SavedCursorPos;
    RECT _SavedClipCursor;
    CONST RECT _DefaultClipCursor;
    bool _ApplicationInputsHidden;
    bool _OverlayInputsHidden;

    // In (bool): Is toggle wanted
    // Out(bool): Is the overlay visible, if true, inputs will be disabled
    std::function<void()> _KeyCombinationCallback;
    std::vector<int> _NativeKeyCombination;
    bool _KeyCombinationPushed;

    // Functions
    WindowsHook_t();
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

    static UINT  WINAPI _MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);
    static UINT  WINAPI _MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
    static SHORT WINAPI _MyGetKeyState(int nVirtKey);
    static SHORT WINAPI _MyGetAsyncKeyState(int vKey);
    static BOOL  WINAPI _MyGetKeyboardState(PBYTE lpKeyState);
    static BOOL  WINAPI _MyGetCursorPos(LPPOINT lpPoint);
    static BOOL  WINAPI _MySetCursorPos(int X, int Y);
    static BOOL  WINAPI _MyGetClipCursor(RECT* lpRect);
    static BOOL  WINAPI _MyClipCursor(CONST RECT* lpRect);
    static BOOL  WINAPI _MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    static BOOL  WINAPI _MyGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    static BOOL  WINAPI _MyPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
    static BOOL  WINAPI _MyPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

    static short _ImGuiGetKeyState(int nVirtKey);
public:
    std::string LibraryName;

    virtual ~WindowsHook_t();

    void ResetRenderState(OverlayHookState state);
    void SetInitialWindowSize(HWND hWnd);
    bool PrepareForOverlay(HWND hWnd);
    std::vector<HWND> FindApplicationHWND(DWORD processId);

    bool StartHook(std::function<void()>& keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static WindowsHook_t* Inst();
    virtual const char* GetLibraryName() const;
};

}//namespace InGameOverlay