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

#include "../Library.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "WindowsGamingInputVTables.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace InGameOverlay {

constexpr decltype(WindowsHook_t::DLL_NAME) WindowsHook_t::DLL_NAME;

WindowsHook_t* WindowsHook_t::_inst = nullptr;
SRWLOCK WindowsHook_t::_CallWndProcLock = SRWLOCK_INIT;

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

static HWND GetCurrentThreadHWND()
{
    HWND hWindow = nullptr;
    DWORD tid = GetCurrentThreadId();

    EnumThreadWindows(
        tid,
        [](HWND hwnd, LPARAM lParam) -> BOOL
        {
            auto* result = reinterpret_cast<HWND*>(lParam);

            if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
            {
                *result = hwnd;
                return FALSE;
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&hWindow));

    return hWindow;
}

bool WindowsHook_t::StartHook(std::function<void()>& keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount)
{
    if (!_Hooked)
    {
        if (!keyCombinationCallback)
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination callback.");
            return false;
        }

        if (toggleKeys == nullptr || toggleKeysCount <= 0)
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination.");
            return false;
        }

        void* hUser32 = GetLibraryHandle(DLL_NAME);
        if (hUser32 == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot find {}", DLL_NAME);
            return false;
        }

        LibraryPath = GetLibraryPath(hUser32);
        LibraryName = LibraryPath.string();

        LibraryWrapper library(LibraryPath);
        if (!library.IsValid())
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot load {}", LibraryPath);
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
            *entry.func_ptr = library.GetSymbol(entry.func_name);
            if (*entry.func_ptr == nullptr)
            {
                INGAMEOVERLAY_ERROR("Failed to hook Windows: failed to load function {}.", entry.func_name);
                return false;
            }
        }

        _StartWGIHook();

        INGAMEOVERLAY_INFO("Hooked Windows");
        auto keyCombination = std::make_shared<WindowsHookKeyCombination_t>();
        keyCombination->callback = std::move(keyCombinationCallback);

        for (int i = 0; i < toggleKeysCount; ++i)
        {
            uint32_t k = ToggleKeyToNativeKey(toggleKeys[i]);
            if (k != 0 && std::find(keyCombination->keys.begin(), keyCombination->keys.end(), k) == keyCombination->keys.end())
                keyCombination->keys.emplace_back(k);
        }
        std::atomic_store(&_KeyCombination, std::shared_ptr<const WindowsHookKeyCombination_t>(std::move(keyCombination)));

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
    if (hide && !_ApplicationInputsHidden.load(std::memory_order_acquire))
    {
        // While hidden, the application gets the cursor position and clipping it had now. Saved here,
        // before the clipping changes, whether hidden by the key combination or by the API.
        POINT savedCursorPos;
        if (_GetCursorPos != nullptr && _GetCursorPos(&savedCursorPos))
        {
            _SavedCursorX.store(savedCursorPos.x, std::memory_order_relaxed);
            _SavedCursorY.store(savedCursorPos.y, std::memory_order_relaxed);
        }
        RECT savedClipCursor;
        if (_GetClipCursor != nullptr && _GetClipCursor(&savedClipCursor))
            _StoreSavedClipCursor(savedClipCursor);
    }

    _ApplicationInputsHidden = hide;
    if (hide)
    {
        _ClipCursor(&_DefaultClipCursor);
    }
    else
    {
        RECT savedClipCursor = _LoadSavedClipCursor();
        _ClipCursor(&savedClipCursor);
    }
}

void WindowsHook_t::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void WindowsHook_t::ResetRenderState(OverlayHookState state)
{
    _RemoveCallWndProcHook();
    _WindowEvents.Invalidate();
    if (!_Initialized)
        return;

    _Initialized = false;

    HideAppInputs(false);
    HideOverlayInputs(true);
    _OverlayInputsHiddenProcessed = true;

    ImGui_ImplWin32_Shutdown();
}

bool WindowsHook_t::SetInitialWindowSize(NativeWindow_t const& window)
{
    HWND hWnd = window.Window;
    if (hWnd == nullptr)
    {
        hWnd = GetCurrentThreadHWND();
        if (hWnd == nullptr)
            return false;
    }

    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(hWnd, &rect);
    ImGui::GetIO().DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
    return true;
}

short WindowsHook_t::_ImGuiGetKeyState(int nVirtKey)
{
    return WindowsHook_t::Inst()->_GetKeyState(nVirtKey);
}

bool WindowsHook_t::PrepareForOverlay(NativeWindow_t const& window)
{
    HWND hWnd = window.Window;
    // hWnd can be null when the client code is not using a swapchain for a HWND.
    // In that case, we will run some 'degraded' mode because we cannot make sure the HWND didn't change.
    // TODO: Handle correctly the Composition mode of DirectX.
    if (hWnd != nullptr && _GameHwnd != hWnd)
    {
        ResetRenderState(OverlayHookState::Removing);
    }

    if (!_Initialized)
    {
        if (hWnd == nullptr)
        {
            // If the hWnd is null, we still want an overlay, so try to find the HWND of the current thread.
            // Lets hope the current thread running a Present has a HWND
            hWnd = GetCurrentThreadHWND();
            if (hWnd == nullptr)
                return false;
        }

        _GameHwnd = hWnd;
        ImGui_ImplWin32_Init(_GameHwnd, &WindowsHook_t::_ImGuiGetKeyState);

        _Initialized = true;
    }

    // WH_CALLWNDPROC observes sent messages before the WndProc. It only queues
    // scalar data; ImGui remains confined to the thread that consumes events.
    // Failure is non-fatal and retried on the next PrepareForOverlay call.
    _InstallCallWndProcHook(_GameHwnd);

    if (!_Initialized)
        return false;

    _ProcessWindowEvents();

    if (!_OverlayInputsHidden)
    {
        ImGui_ImplWin32_NewFrame();
    }

    return true;
}

std::vector<NativeWindow_t> WindowsHook_t::FindApplicationWindows()
{
    struct
    {
        DWORD pid;
        std::vector<NativeWindow_t> windows;
    } windowParams{
        GetCurrentProcessId()
    };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        if (!IsWindowVisible(hwnd) && !IsIconic(hwnd))
            return TRUE;

        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);

        auto params = reinterpret_cast<decltype(windowParams)*>(lParam);

        if (processId == params->pid)
            params->windows.push_back(NativeWindow_t{ hwnd });

        return TRUE;
    }, reinterpret_cast<LPARAM>(&windowParams));

    return windowParams.windows;
}

/////////////////////////////////////////////////////////////////////////////////////
// Windows window hooks

// Pointer messages (Windows 8), for SDKs targeting an older version.
#ifndef WM_POINTERUPDATE
#define WM_NCPOINTERUPDATE 0x0241
#define WM_NCPOINTERDOWN   0x0242
#define WM_NCPOINTERUP     0x0243
#define WM_POINTERUPDATE   0x0245
#define WM_POINTERDOWN     0x0246
#define WM_POINTERUP       0x0247
#define WM_POINTERENTER    0x0249
#define WM_POINTERLEAVE    0x024A
#define WM_POINTERWHEEL    0x024E
#define WM_POINTERHWHEEL   0x024F
#endif

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
        // Mouse outside the client area (title bar, borders of a windowed game). The hidden messages still go to
        // DefWindowProc: the window can still be moved, resized or closed.
        case WM_NCMOUSEMOVE: case WM_NCMOUSEHOVER: case WM_NCMOUSELEAVE:
        case WM_NCLBUTTONUP: case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONUP: case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
        case WM_NCMBUTTONUP: case WM_NCMBUTTONDOWN: case WM_NCMBUTTONDBLCLK:
        case WM_NCXBUTTONUP: case WM_NCXBUTTONDOWN: case WM_NCXBUTTONDBLCLK:
        // Pointer Events: the mouse comes as WM_POINTER* once the application called EnableMouseInPointer(TRUE), and it
        // reads it with GetPointerInfo. DefWindowProc turns them into the mouse messages above, hidden too, which the
        // overlay gets.
        case WM_POINTERUPDATE: case WM_POINTERDOWN: case WM_POINTERUP:
        case WM_POINTERWHEEL: case WM_POINTERHWHEEL:
        case WM_POINTERENTER: case WM_POINTERLEAVE:
        case WM_NCPOINTERUPDATE: case WM_NCPOINTERDOWN: case WM_NCPOINTERUP:
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

void WindowsHook_t::_AppendEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // lParam may contain caller-owned pointers (for example WM_IME_COMPOSITION).
    // Queue only scalar payloads. WM_SETCURSOR and WM_IME_COMPOSITION are
    // intentionally excluded: their backend paths call native APIs tied to the
    // window thread (cursor update / DefWindowProcW).
    switch (uMsg)
    {
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
    case WM_DESTROY:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONUP: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR: case WM_UNICHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: case WM_IME_CHAR:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_INPUTLANGCHANGE:
    case WM_DEVICECHANGE:
        break;
    default:
        return;
    }

    // These backend paths use only the message and wParam. Their Win32
    // lParam may be an HWND, HKL, or temporary device-notification pointer.
    if (uMsg == WM_SETFOCUS || uMsg == WM_KILLFOCUS ||
        uMsg == WM_INPUTLANGCHANGE || uMsg == WM_DEVICECHANGE)
        lParam = 0;
    // Invalidate queued state on overflow; the render thread clears ImGui state.
    _WindowEvents.Enqueue(hWnd, uMsg, wParam, lParam);
}

void WindowsHook_t::_ProcessWindowEvents()
{
    ImGuiIO& io = ImGui::GetIO();
    auto resetInputState = [&io]()
    {
        // Clear pending Add* events before clearing state, or a queued key-down
        // could be replayed by NewFrame after the state reset.
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
        io.AddFocusEvent(true);
        io.SetAppAcceptingEvents(true);
    };

    size_t eventCount = 0;
    uint64_t batchGeneration = 0;
    if (_WindowEvents.TakeBatch(eventCount, batchGeneration))
        resetInputState();
    const bool overlayInputsHidden = _OverlayInputsHidden.load(std::memory_order_acquire);
    if (overlayInputsHidden && !_OverlayInputsHiddenProcessed)
        resetInputState();
    _OverlayInputsHiddenProcessed = overlayInputsHidden;
    for (size_t i = 0; i < eventCount; ++i)
    {
        if (!_WindowEvents.IsCurrent(batchGeneration))
        {
            resetInputState();
            break;
        }
        const WindowsHookEvent_t& event = _WindowEvents.GetBatchEvent(i);
        if (event.hWnd != _GameHwnd.load(std::memory_order_acquire))
            continue;

        const bool focusEvent = event.msg == WM_SETFOCUS || event.msg == WM_KILLFOCUS;
        if (_OverlayInputsHidden.load(std::memory_order_acquire) && !focusEvent)
            continue;
        if (focusEvent)
            io.SetAppAcceptingEvents(event.msg == WM_SETFOCUS);
        ImGui_ImplWin32_WndProcHandler(event.hWnd, event.msg, event.wParam, event.lParam);
        // Invalidation can race the handler. Clear any state it submitted
        // before another event can be considered.
        if (!_WindowEvents.IsCurrent(batchGeneration))
            resetInputState();
        else if (_OverlayInputsHidden.load(std::memory_order_acquire))
        {
            resetInputState();
            if (focusEvent)
            {
                const bool focused = event.msg == WM_SETFOCUS;
                io.SetAppAcceptingEvents(focused);
                io.AddFocusEvent(focused);
            }
            _OverlayInputsHiddenProcessed = true;
        }
    }

    if (!_WindowEvents.IsCurrent(batchGeneration))
        resetInputState();
}

RECT WindowsHook_t::_LoadSavedClipCursor() const
{
    // Each edge is race-free, but independent atomics do not form a
    // transactionally consistent RECT if multiple threads update it at once.
    return RECT{
        _SavedClipLeft.load(std::memory_order_relaxed),
        _SavedClipTop.load(std::memory_order_relaxed),
        _SavedClipRight.load(std::memory_order_relaxed),
        _SavedClipBottom.load(std::memory_order_relaxed)
    };
}

void WindowsHook_t::_StoreSavedClipCursor(const RECT& rect)
{
    _SavedClipLeft.store(rect.left, std::memory_order_relaxed);
    _SavedClipTop.store(rect.top, std::memory_order_relaxed);
    _SavedClipRight.store(rect.right, std::memory_order_relaxed);
    _SavedClipBottom.store(rect.bottom, std::memory_order_relaxed);
}

bool WindowsHook_t::_InstallCallWndProcHook(HWND hWnd)
{
    DWORD threadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (threadId == 0)
        return false;

    AcquireSRWLockExclusive(&_CallWndProcLock);

    if (_CallWndProcHook != nullptr && _CallWndProcHwnd == hWnd && _CallWndProcThreadId == threadId)
    {
        ReleaseSRWLockExclusive(&_CallWndProcLock);
        return true;
    }

    if (_CallWndProcHook != nullptr)
    {
        UnhookWindowsHookEx(_CallWndProcHook);
        _CallWndProcHook = nullptr;
    }

    _CallWndProcHwnd = hWnd;
    _CallWndProcThreadId = threadId;
    _CallWndProcHook = SetWindowsHookExW(WH_CALLWNDPROC, &_CallWndProc, nullptr, threadId);
    bool installed = _CallWndProcHook != nullptr;

    ReleaseSRWLockExclusive(&_CallWndProcLock);

    if (!installed)
        INGAMEOVERLAY_WARN("Failed to install WH_CALLWNDPROC for window {} (thread {}).", (void*)hWnd, threadId);

    return installed;
}

void WindowsHook_t::_RemoveCallWndProcHook()
{
    AcquireSRWLockExclusive(&_CallWndProcLock);

    if (_CallWndProcHook != nullptr)
    {
        UnhookWindowsHookEx(_CallWndProcHook);
        _CallWndProcHook = nullptr;
    }

    _CallWndProcHwnd = nullptr;
    _CallWndProcThreadId = 0;

    ReleaseSRWLockExclusive(&_CallWndProcLock);
}

LRESULT CALLBACK WindowsHook_t::_CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message != nullptr)
        {
            AcquireSRWLockShared(&_CallWndProcLock);
            WindowsHook_t* inst = _inst;
            if (inst != nullptr && message->hwnd == inst->_CallWndProcHwnd)
            {
                switch (message->message)
                {
                case WM_SETFOCUS:
                case WM_KILLFOCUS:
                case WM_INPUTLANGCHANGE:
                case WM_DEVICECHANGE:
                    // lParam can contain an ephemeral pointer for device changes.
                    // ImGui only consumes wParam for WM_DEVICECHANGE; the other
                    // messages do not need lParam either.
                    inst->_AppendEvent(message->hwnd, message->message, message->wParam, 0);
                    break;
                // WM_SETCURSOR requires a synchronous WndProc result and may call
                // SetCursor. Keep it on the host path; NewFrame updates ImGui's
                // requested cursor when it changes.
                }
            }
            ReleaseSRWLockShared(&_CallWndProcLock);
        }
    }

    // WH_CALLWNDPROC sees messages sent to the WndProc, including same-thread
    // SendMessage. Posted messages are observed by the existing Get/Peek hooks.
    // Do not use InSendMessageEx here: same-thread sends are not reported by it.
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void WindowsHook_t::_RawEvent(RAWINPUT& raw)
{
    switch (raw.header.dwType)
    {
    case RIM_TYPEMOUSE:
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_LBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_LBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_RBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_RBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_MBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_MBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
            _AppendEvent(_GameHwnd, WM_MOUSEWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_HWHEEL)
            _AppendEvent(_GameHwnd, WM_MOUSEHWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);

        if (raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0)
        {
            POINT p;
            _GetCursorPos(&p);
            ::ScreenToClient(_GameHwnd, &p);
            _AppendEvent(_GameHwnd, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        }
        break;

    //case RIM_TYPEKEYBOARD:
        //_AppendEvent(_GameHwnd, raw.data.keyboard.Message, raw.data.keyboard.VKey, 0);
        //break;
    }
}

// Same as NEXTRAWINPUTBLOCK, which relies on a QWORD type the Windows headers don't declare.
static PRAWINPUT NextRawInputBlock(PRAWINPUT pRawInput)
{
    constexpr ULONG_PTR alignment = sizeof(ULONG_PTR);
    const ULONG_PTR next = reinterpret_cast<ULONG_PTR>(pRawInput) + pRawInput->header.dwSize;
    return reinterpret_cast<PRAWINPUT>((next + alignment - 1) & ~(alignment - 1));
}

void WindowsHook_t::_LatchKey(UINT vk)
{
    if (vk < 256)
        _LatchedKeys[vk / 64].fetch_or(uint64_t(1) << (vk % 64), std::memory_order_acq_rel);
}

void WindowsHook_t::_UnlatchKey(UINT vk)
{
    if (vk < 256)
        _LatchedKeys[vk / 64].fetch_and(~(uint64_t(1) << (vk % 64)), std::memory_order_acq_rel);
}

bool WindowsHook_t::_IsKeyLatched(UINT vk) const
{
    // The key combination uses the generic modifiers, the application may ask for the left or right one.
    switch (vk)
    {
        case VK_LSHIFT: case VK_RSHIFT: vk = VK_SHIFT; break;
        case VK_LCONTROL: case VK_RCONTROL: vk = VK_CONTROL; break;
        case VK_LMENU: case VK_RMENU: vk = VK_MENU; break;
    }

    return vk < 256 && (_LatchedKeys[vk / 64].load(std::memory_order_acquire) & (uint64_t(1) << (vk % 64))) != 0;
}

bool WindowsHook_t::_AnyKeyLatched() const
{
    for (auto const& keys : _LatchedKeys)
    {
        if (keys.load(std::memory_order_acquire) != 0)
            return true;
    }
    return false;
}

bool WindowsHook_t::_HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    const bool was_app_inputs_hidden = _ApplicationInputsHidden.load(std::memory_order_acquire);
    const bool was_overlay_inputs_hidden = _OverlayInputsHidden.load(std::memory_order_acquire);
    bool hide_app_inputs = was_app_inputs_hidden;
    bool overlay_inputs_hidden_after_callback = was_overlay_inputs_hidden;
    auto keyCombination = std::atomic_load(&_KeyCombination);
    
    if (_Initialized)
    {
        // Is the event is a key press
        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
        {
            int key_count = 0;
            for (auto const& key : keyCombination->keys)
            {
                if (_GetAsyncKeyState(key) & (1 << 15))
                    ++key_count;
            }
    
            if (key_count == keyCombination->keys.size())
            {// All shortcut keys are pressed
                bool expected = false;
                if (_KeyCombinationPushed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    for (auto const& key : keyCombination->keys)
                        _LatchKey(key);

                    keyCombination->callback();
                    const bool appInputsHiddenNow = _ApplicationInputsHidden.load(std::memory_order_acquire);
                    const bool overlayInputsHiddenNow = _OverlayInputsHidden.load(std::memory_order_acquire);
                    overlay_inputs_hidden_after_callback = overlayInputsHiddenNow;
                    if (appInputsHiddenNow)
                    {
                        // HideAppInputs saved the cursor position and clipping.
                        hide_app_inputs = true;
                    }
                    else
                    {
                        RECT savedClipCursor = _LoadSavedClipCursor();
                        _ClipCursor(&savedClipCursor);
                    }
                }
            }
            else
            {
                _KeyCombinationPushed.store(false, std::memory_order_release);
            }
        }
    
        const bool overlay_hidden_now = _OverlayInputsHidden.load(std::memory_order_acquire);
        if ((!was_overlay_inputs_hidden && !overlay_inputs_hidden_after_callback && !overlay_hidden_now) ||
            uMsg == WM_KILLFOCUS || uMsg == WM_SETFOCUS)
            _AppendEvent(hWnd, uMsg, wParam, lParam);
    
        if (hide_app_inputs && IgnoreMsg(uMsg))
        {
            if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
                _UnlatchKey(static_cast<UINT>(wParam));
            return true;
        }

        // The key combination's own messages, until each key is released.
        if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && _IsKeyLatched(static_cast<UINT>(wParam)))
        {
            if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
                _UnlatchKey(static_cast<UINT>(wParam));
            return true;
        }
    }

    return false;
}

UINT WindowsHook_t::_RemoveLatchedRawInput(PRAWINPUT pData, UINT count) const
{
    // Blocks have a variable size and are pointer aligned: move the kept ones down.
    PRAWINPUT read = pData;
    PRAWINPUT write = pData;
    UINT kept = 0;
    for (UINT i = 0; i < count; ++i)
    {
        PRAWINPUT next = NextRawInputBlock(read);
        if (read->header.dwType != RIM_TYPEKEYBOARD || !_IsKeyLatched(read->data.keyboard.VKey))
        {
            if (write != read)
                memmove(write, read, read->header.dwSize);
            write = NextRawInputBlock(write);
            ++kept;
        }
        read = next;
    }
    return kept;
}


UINT WINAPI WindowsHook_t::_MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    int res = inst->_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized)
        return res;

    if (!inst->_OverlayInputsHidden && pData != nullptr)
    {
        // RAWINPUT blocks have a variable size (dwSize), they must not be walked as an array.
        PRAWINPUT pRawInput = pData;
        for (int i = 0; i < res; ++i)
        {
            inst->_RawEvent(*pRawInput);
            pRawInput = NextRawInputBlock(pRawInput);
        }
    }

    if (!inst->_ApplicationInputsHidden)
    {
        if (res > 0 && pData != nullptr && inst->_AnyKeyLatched())
            res = static_cast<int>(inst->_RemoveLatchedRawInput(pData, static_cast<UINT>(res)));
        return res;
    }

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

    // A keyboard RAWINPUT is smaller than sizeof(RAWINPUT), which is sized for the largest device data.
    const bool latchedKey = uiCommand == RID_INPUT && res != static_cast<UINT>(-1) && res >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD) &&
        reinterpret_cast<RAWINPUT*>(pData)->header.dwType == RIM_TYPEKEYBOARD &&
        inst->_IsKeyLatched(reinterpret_cast<RAWINPUT*>(pData)->data.keyboard.VKey);

    if (!inst->_ApplicationInputsHidden && !latchedKey)
        return res;

    memset(pData, 0, *pcbSize);
    *pcbSize = 0;
    return 0;
}

SHORT WINAPI WindowsHook_t::_MyGetKeyState(int nVirtKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && (inst->_ApplicationInputsHidden || inst->_IsKeyLatched(static_cast<UINT>(nVirtKey))))
        return 0;

    return inst->_GetKeyState(nVirtKey);
}

SHORT WINAPI WindowsHook_t::_MyGetAsyncKeyState(int vKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && (inst->_ApplicationInputsHidden || inst->_IsKeyLatched(static_cast<UINT>(vKey))))
        return 0;

    return inst->_GetAsyncKeyState(vKey);
}

BOOL WINAPI WindowsHook_t::_MyGetKeyboardState(PBYTE lpKeyState)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
    {
        // Callers rarely check the result: give them released keys rather than an untouched buffer.
        if (lpKeyState == nullptr)
            return FALSE;

        memset(lpKeyState, 0, 256);
        return TRUE;
    }

    const BOOL res = inst->_GetKeyboardState(lpKeyState);
    if (res && lpKeyState != nullptr && inst->_Initialized && inst->_AnyKeyLatched())
    {
        for (UINT vk = 0; vk < 256; ++vk)
        {
            if (inst->_IsKeyLatched(vk))
                lpKeyState[vk] = 0;
        }
    }
    return res;
}

BOOL  WINAPI WindowsHook_t::_MyGetCursorPos(LPPOINT lpPoint)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    BOOL res = inst->_GetCursorPos(lpPoint);
    if (inst->_Initialized && inst->_ApplicationInputsHidden && lpPoint != nullptr)
    {
        lpPoint->x = inst->_SavedCursorX.load(std::memory_order_relaxed);
        lpPoint->y = inst->_SavedCursorY.load(std::memory_order_relaxed);
    }

    return res;
}

BOOL WINAPI WindowsHook_t::_MySetCursorPos(int X, int Y)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_SetCursorPos(X, Y);

    // The cursor stays for the overlay, but the application must read back the position it set:
    // a mouse look recenters the cursor every frame and turns by the difference.
    inst->_SavedCursorX.store(X, std::memory_order_relaxed);
    inst->_SavedCursorY.store(Y, std::memory_order_relaxed);
    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyGetClipCursor(RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    if (lpRect == nullptr || !inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_GetClipCursor(lpRect);

    *lpRect = inst->_LoadSavedClipCursor();
    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyClipCursor(CONST RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    CONST RECT* v = lpRect == nullptr ? &inst->_DefaultClipCursor : lpRect;

    inst->_StoreSavedClipCursor(*v);

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_ClipCursor(v);
    
    return inst->_ClipCursor(&inst->_DefaultClipCursor);
}

BOOL WINAPI WindowsHook_t::_MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
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
    BOOL res = inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE))
    {
        if (!inst->_ApplicationInputsHidden || !IgnoreMsg(lpMsg->message))
            return res;

        // Remove the message that will be hidden from the application and handle the
        // MSG returned by the removal, since another thread may have changed the queue.
        if (!inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg | PM_REMOVE))
        {
            lpMsg->message = WM_NULL;
            return res;
        }
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
    BOOL res = inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE))
    {
        if (!inst->_ApplicationInputsHidden || !IgnoreMsg(lpMsg->message))
            return res;

        // Remove the message that will be hidden from the application and handle the
        // MSG returned by the removal, since another thread may have changed the queue.
        if (!inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg | PM_REMOVE))
        {
            lpMsg->message = WM_NULL;
            return res;
        }
    }

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageW(lpMsg);
    inst->_DefWindowProcW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

// Windows::Gaming::Input
void WindowsHook_t::_StartWGIHook()
{
    _RawControllerStatics = SimpleWindowsGamingInput::GetRawGameControllerStatics();
    _GamepadStatics = SimpleWindowsGamingInput::GetGamepadStatics();
    
    if (_RawControllerStatics == nullptr || _GamepadStatics == nullptr)
        return;

    auto hookedRaw = false;
    SimpleWindowsGamingInput::VectorView<SimpleWindowsGamingInput::IRawGameController*>* rawControllers = nullptr;
    if (SUCCEEDED(_RawControllerStatics->get_RawGameControllers(&rawControllers)) && rawControllers != nullptr)
    {
        unsigned int s;
        if (SUCCEEDED(rawControllers->get_Size(&s)) && s > 0)
        {
            SimpleWindowsGamingInput::IRawGameController* rawController = nullptr;
            if (SUCCEEDED(rawControllers->GetAt(0, &rawController)) && rawController != nullptr)
            {
                _StartRawControllerHook(rawController);
                hookedRaw = true;

                rawController->Release();
            }
        }

        rawControllers->Release();
    }

    auto hookedGamepad = false;
    SimpleWindowsGamingInput::VectorView<SimpleWindowsGamingInput::IGamepad*>* gamepads = nullptr;
    if (SUCCEEDED(_GamepadStatics->get_Gamepads(&gamepads)) && gamepads != nullptr)
    {
        unsigned int s;
        if (SUCCEEDED(gamepads->get_Size(&s)) && s > 0)
        {
            SimpleWindowsGamingInput::IGamepad* gamepad = nullptr;
            if (SUCCEEDED(gamepads->GetAt(0, &gamepad)) && gamepad != nullptr)
            {
                _StartGamepadHook(gamepad);
                hookedGamepad = true;

                gamepad->Release();
            }
        }

        gamepads->Release();
    }

    if (!hookedRaw)
        _RawControllerStatics->add_RawGameControllerAdded(&_RawControllerAddedHandler, &_OnRawControllerAddedToken);
    
    if (!hookedGamepad)
        _GamepadStatics->add_GamepadAdded(&_GamepadAddedHandler, &_OnGamepadAddedToken);
}

void WindowsHook_t::_StartRawControllerHook(SimpleWindowsGamingInput::IRawGameController* pRawController)
{
    void** vtable = *(void***)pRawController;
    *(void**)&_RawControllerGetCurrentReading = vtable[(int)IRawGameControllerVTable::GetCurrentReading];

    BeginHook();

    if (!HookFunc(std::make_pair((void**)&_RawControllerGetCurrentReading, (void*)&WindowsHook_t::_MyRawControllerGetCurrentReading)))
    {
        INGAMEOVERLAY_ERROR("Failed to hook {}", "IRawGameController::GetCurrentReading");
    }

    EndHook();
}

void WindowsHook_t::_StartGamepadHook(SimpleWindowsGamingInput::IGamepad* pGamepad)
{
    void** vtable = *(void***)pGamepad;
    *(void**)&_GamepadGetCurrentReading = vtable[(int)IGamepadVTable::GetCurrentReading];

    BeginHook();

    if (!HookFunc(std::make_pair((void**)&_GamepadGetCurrentReading, (void*)&WindowsHook_t::_MyGamepadGetCurrentReading)))
    {
        INGAMEOVERLAY_ERROR("Failed to hook {}", "IGamepad::GetCurrentReading");
    }

    EndHook();
}

HRESULT STDMETHODCALLTYPE WindowsHook_t::_MyRawControllerGetCurrentReading(SimpleWindowsGamingInput::IRawGameController* _this, UINT32 buttonArrayLength, boolean* buttonArray, UINT32 switchArrayLength, SimpleWindowsGamingInput::GameControllerSwitchPosition* switchArray, UINT32 axisArrayLength, DOUBLE* axisArray, UINT64* timestamp)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    auto result = (_this->*inst->_RawControllerGetCurrentReading)(buttonArrayLength, buttonArray, switchArrayLength, switchArray, axisArrayLength, axisArray, timestamp);

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return result;

    if (buttonArray != nullptr)
        memset(buttonArray, 0, sizeof(*buttonArray) * buttonArrayLength);

    if (switchArray != nullptr)
        memset(switchArray, 0, sizeof(*switchArray) * switchArrayLength);

    if (axisArray != nullptr)
        memset(axisArray, 0, sizeof(*axisArray) * axisArrayLength);

    if (timestamp != nullptr)
        *timestamp = 0;

    return result;
}

HRESULT STDMETHODCALLTYPE WindowsHook_t::_MyGamepadGetCurrentReading(SimpleWindowsGamingInput::IGamepad* _this, SimpleWindowsGamingInput::GamepadReading* value)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    auto result = (_this->*inst->_GamepadGetCurrentReading)(value);

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return result;

    if (value != nullptr)
        memset(value, 0, sizeof(*value));

    return result;
}

HRESULT WindowsHook_t::_OnRawControllerAdded(SimpleWindowsGamingInput::IInspectable*, SimpleWindowsGamingInput::IRawGameController* pRawController)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    inst->_RawControllerStatics->remove_RawGameControllerAdded(inst->_OnRawControllerAddedToken);
    inst->_OnRawControllerAddedToken = {};

    inst->_StartRawControllerHook(pRawController);

    return S_OK;
}

HRESULT WindowsHook_t::_OnGamepadAdded(SimpleWindowsGamingInput::IInspectable*, SimpleWindowsGamingInput::IGamepad* pGamepad)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    inst->_GamepadStatics->remove_GamepadAdded(inst->_OnGamepadAddedToken);
    inst->_OnGamepadAddedToken = {};

    inst->_StartGamepadHook(pGamepad);

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////////////

WindowsHook_t::WindowsHook_t() :
    _Hooked(false),
    _Initialized(false),
    _GameHwnd(nullptr),
    _CallWndProcHook(nullptr),
    _CallWndProcThreadId(0),
    _CallWndProcHwnd(nullptr),
    _SavedCursorX(0),
    _SavedCursorY(0),
    _SavedClipLeft(LONG(0xFFFF8000)),
    _SavedClipTop(LONG(0xFFFF8000)),
    _SavedClipRight(LONG(0x00007FFF)),
    _SavedClipBottom(LONG(0x00007FFF)),
    _DefaultClipCursor{ LONG(0xFFFF8000), LONG(0xFFFF8000), LONG(0x00007FFF), LONG(0x00007FFF) },
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _OverlayInputsHiddenProcessed(true),
    _KeyCombination(std::make_shared<WindowsHookKeyCombination_t>()),
    _KeyCombinationPushed(false),
    _LatchedKeys{},
    _WindowEvents(512),
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
    _PeekMessageW(nullptr),
    // WGI
    _OnRawControllerAddedToken({}),
    _OnGamepadAddedToken({}),
    _RawControllerAddedHandler(&WindowsHook_t::_OnRawControllerAdded),
    _GamepadAddedHandler(&WindowsHook_t::_OnGamepadAdded),
    _RawControllerGetCurrentReading(nullptr),
    _GamepadGetCurrentReading(nullptr)
{
}

WindowsHook_t::~WindowsHook_t()
{
    INGAMEOVERLAY_INFO("Windows Hook removed");

    ResetRenderState(OverlayHookState::Removing);

    //if (_OnRawControllerAddedToken.value != 0)
    //{
    //    _RawControllerStatics->remove_RawGameControllerAdded(_OnRawControllerAddedToken);
    //    _OnRawControllerAddedToken = {};
    //    _RawControllerAddedHandler.ReleaseAndGetAddressOf();
    //}
    //if (_OnGamepadAddedToken.value != 0)
    //{
    //    _GamepadStatics->remove_GamepadAdded(_OnGamepadAddedToken);
    //    _OnGamepadAddedToken = {};
    //    _GamepadAddedHandler.ReleaseAndGetAddressOf();
    //}

    UnhookAll();
    AcquireSRWLockExclusive(&_CallWndProcLock);
    _inst = nullptr;
    ReleaseSRWLockExclusive(&_CallWndProcLock);
}

WindowsHook_t* WindowsHook_t::Inst()
{
    AcquireSRWLockExclusive(&_CallWndProcLock);
    if (_inst == nullptr)
        _inst = new WindowsHook_t;

    WindowsHook_t* inst = _inst;
    ReleaseSRWLockExclusive(&_CallWndProcLock);
    return inst;
}

const char* WindowsHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

}//namespace InGameOverlay
