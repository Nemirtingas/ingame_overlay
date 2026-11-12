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

// An OpenGL host for the Windows window and OpenGL hooks: it loads an overlay library and logs what the
// application itself sees, so that tests/windows_hook_tests/check_scenarios.py can check what the hooks hide.
// Usage: windows_hook_tests_app [overlay library]
//
// Options, from the environment:
//   INGAMEOVERLAY_TEST_LOG=<file>          Log the application's view, one event per line (see below).
//   INGAMEOVERLAY_TEST_MOUSE_LOOK=1        Read the cursor offset from the client center, then recenter it, every frame.
//   INGAMEOVERLAY_TEST_CLIP_CURSOR=1       Confine the cursor to the client area.
//   INGAMEOVERLAY_TEST_RAW_KEYBOARD=1      Register the keyboard for raw input, read with GetRawInputData on WM_INPUT.
//   INGAMEOVERLAY_TEST_RAW_KEYBOARD=buffer Same, read with GetRawInputBuffer every frame.
//   INGAMEOVERLAY_TEST_GL_PIXEL_STATE=1    Leave pixel buffers bound and a non default pixel store state before each swap.
//   INGAMEOVERLAY_TEST_GL_ERROR=1          Leave a GL error before each swap.
//   INGAMEOVERLAY_TEST_BACKGROUND=1        Show the window without activating it, for the scenarios without input.
//   INGAMEOVERLAY_TEST_POINTER=1           Call EnableMouseInPointer(TRUE): the mouse comes as WM_POINTER* messages.
//
// Frames are paced (about 100 per second) like a game, which makes the input scenarios reproducible.
//
// Log lines, "<GetTickCount64> <event> ...":
//   start <client width> <client height>
//   msg down|up <virtual key> [repeat]     WM_KEYDOWN/WM_SYSKEYDOWN, WM_KEYUP/WM_SYSKEYUP
//   raw down|up <virtual key>              raw keyboard input
//   async|state <virtual key> down|up      GetAsyncKeyState/GetKeyState changes of the watched keys
//   kbstate <result> <written> <watched keys down>   GetKeyboardState changes: result, whether the buffer was written
//   look <dx> <dy>                         cursor offset from the client center (mouse look)
//   appclip <left> <top> <right> <bottom>  the clipping the application set, again at each activation like games do
//   clip <left> <top> <right> <bottom>     GetClipCursor changes
//   mouse|ncmouse|pointer <message>        mouse messages in the client area, outside it (title bar, borders), and
//                                          WM_POINTER* messages: move, ldown, lup, rdown, wheel, update, enter...
//                                          received by the window procedure
//   queued mouse|ncmouse|pointer <message> the same messages, taken from the message queue by PeekMessage

#include <windows.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static FILE* g_Log = nullptr;
static HDC g_DC = nullptr;
static HGLRC g_RC = nullptr;
static bool g_RawBuffer = false;
static bool g_ClipCursor = false;

// Keys watched for state changes: the default overlay key combination and a few others.
static const int WatchedKeys[] = { VK_SHIFT, VK_F2, VK_F3, 'W' };

static void Log(const char* format, ...)
{
    if (g_Log == nullptr)
        return;

    fprintf(g_Log, "%llu ", GetTickCount64());
    va_list args;
    va_start(args, format);
    vfprintf(g_Log, format, args);
    va_end(args);
    fputc('\n', g_Log);
    fflush(g_Log);
}

static bool Option(const char* name)
{
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0' && strcmp(value, "0") != 0;
}

// Category and name of the mouse messages the application logs, or nullptr.
static const char* MouseMessageName(UINT msg, const char** category)
{
    struct { UINT msg; const char* category; const char* name; } static const names[] = {
        { WM_MOUSEMOVE, "mouse", "move" }, { WM_MOUSEWHEEL, "mouse", "wheel" }, { WM_MOUSEHWHEEL, "mouse", "hwheel" },
        { WM_LBUTTONDOWN, "mouse", "ldown" }, { WM_LBUTTONUP, "mouse", "lup" }, { WM_LBUTTONDBLCLK, "mouse", "ldblclk" },
        { WM_RBUTTONDOWN, "mouse", "rdown" }, { WM_RBUTTONUP, "mouse", "rup" },
        { WM_MBUTTONDOWN, "mouse", "mdown" }, { WM_MBUTTONUP, "mouse", "mup" },
        { WM_XBUTTONDOWN, "mouse", "xdown" }, { WM_XBUTTONUP, "mouse", "xup" },
        { WM_NCMOUSEMOVE, "ncmouse", "move" }, { WM_NCMOUSEHOVER, "ncmouse", "hover" }, { WM_NCMOUSELEAVE, "ncmouse", "leave" },
        { WM_NCLBUTTONDOWN, "ncmouse", "ldown" }, { WM_NCLBUTTONUP, "ncmouse", "lup" }, { WM_NCLBUTTONDBLCLK, "ncmouse", "ldblclk" },
        { WM_NCRBUTTONDOWN, "ncmouse", "rdown" }, { WM_NCRBUTTONUP, "ncmouse", "rup" },
        { WM_NCMBUTTONDOWN, "ncmouse", "mdown" }, { WM_NCMBUTTONUP, "ncmouse", "mup" },
        { WM_NCXBUTTONDOWN, "ncmouse", "xdown" }, { WM_NCXBUTTONUP, "ncmouse", "xup" },
        { WM_POINTERUPDATE, "pointer", "update" }, { WM_POINTERDOWN, "pointer", "down" }, { WM_POINTERUP, "pointer", "up" },
        { WM_POINTERWHEEL, "pointer", "wheel" }, { WM_POINTERHWHEEL, "pointer", "hwheel" },
        { WM_POINTERENTER, "pointer", "enter" }, { WM_POINTERLEAVE, "pointer", "leave" },
        { WM_NCPOINTERUPDATE, "pointer", "ncupdate" }, { WM_NCPOINTERDOWN, "pointer", "ncdown" }, { WM_NCPOINTERUP, "pointer", "ncup" },
    };
    for (auto const& entry : names)
    {
        if (entry.msg == msg)
        {
            *category = entry.category;
            return entry.name;
        }
    }
    return nullptr;
}

static void LogRawKeyboard(RAWINPUT const& raw)
{
    if (raw.header.dwType == RIM_TYPEKEYBOARD)
        Log("raw %s %u", (raw.data.keyboard.Flags & RI_KEY_BREAK) ? "up" : "down", (unsigned)raw.data.keyboard.VKey);
}

static RECT ClientRectOnScreen(HWND hWnd)
{
    RECT rect;
    GetClientRect(hWnd, &rect);
    POINT topLeft{ rect.left, rect.top };
    POINT bottomRight{ rect.right, rect.bottom };
    ClientToScreen(hWnd, &topLeft);
    ClientToScreen(hWnd, &bottomRight);
    return RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
}

// Windows resets the clipping when another window takes the foreground: games clip again on activation.
static void ClipToClient(HWND hWnd)
{
    const RECT clip = ClientRectOnScreen(hWnd);
    ClipCursor(&clip);
    Log("appclip %ld %ld %ld %ld", clip.left, clip.top, clip.right, clip.bottom);
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const char* category;
    if (const char* name = MouseMessageName(msg, &category))
        Log("%s %s", category, name);

    switch (msg)
    {
        case WM_ACTIVATE:
            if (g_ClipCursor && LOWORD(wParam) != WA_INACTIVE)
                ClipToClient(hWnd);
            break;

        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            Log("msg down %u%s", (unsigned)wParam, (lParam & (1 << 30)) ? " repeat" : "");
            break;

        case WM_KEYUP: case WM_SYSKEYUP:
            Log("msg up %u", (unsigned)wParam);
            break;

        case WM_INPUT:
            if (!g_RawBuffer)
            {
                RAWINPUT raw{};
                UINT size = sizeof(raw);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && size != 0)
                    LogRawKeyboard(raw);
            }
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool CreateContext(HWND hWnd)
{
    g_DC = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    const int pixelFormat = ChoosePixelFormat(g_DC, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(g_DC, pixelFormat, &pfd))
        return false;

    g_RC = wglCreateContext(g_DC);
    return g_RC != nullptr && wglMakeCurrent(g_DC, g_RC) && gladLoaderLoadGL() != 0;
}

// Leaves what an engine may leave bound or set at the end of its frame.
static void SetHostilePixelState()
{
    static GLuint buffer = 0;
    if (buffer == 0)
    {
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, 64, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    glPixelStorei(GL_PACK_ROW_LENGTH, 4096);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
}

int main(int argc, char** argv)
{
    const char* overlayLibrary = argc > 1 ? argv[1] : "overlay_example.dll";

    if (const char* logPath = getenv("INGAMEOVERLAY_TEST_LOG"))
        g_Log = fopen(logPath, "w");

    const bool mouseLook = Option("INGAMEOVERLAY_TEST_MOUSE_LOOK");
    const bool clipCursor = Option("INGAMEOVERLAY_TEST_CLIP_CURSOR");
    g_ClipCursor = clipCursor;
    const bool rawKeyboard = Option("INGAMEOVERLAY_TEST_RAW_KEYBOARD");
    g_RawBuffer = rawKeyboard && strcmp(getenv("INGAMEOVERLAY_TEST_RAW_KEYBOARD"), "buffer") == 0;
    const bool hostilePixelState = Option("INGAMEOVERLAY_TEST_GL_PIXEL_STATE");
    const bool leaveGLError = Option("INGAMEOVERLAY_TEST_GL_ERROR");

    SetProcessDPIAware();

    if (Option("INGAMEOVERLAY_TEST_POINTER") && !EnableMouseInPointer(TRUE))
        Log("pointer_enable_failed %lu", GetLastError());

    WNDCLASSEXW wc{ sizeof(wc), CS_OWNDC, WndProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"InGameOverlayHookTests", nullptr };
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowW(wc.lpszClassName, L"InGameOverlay Windows hook tests", WS_OVERLAPPEDWINDOW, 100, 100, 1024, 640, nullptr, nullptr, wc.hInstance, nullptr);
    if (hWnd == nullptr || !CreateContext(hWnd))
        return 1;

    ShowWindow(hWnd, Option("INGAMEOVERLAY_TEST_BACKGROUND") ? SW_SHOWNOACTIVATE : SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    if (rawKeyboard)
    {
        RAWINPUTDEVICE device{ 0x01, 0x06, 0, hWnd };
        RegisterRawInputDevices(&device, 1, sizeof(device));
    }

    RECT client;
    GetClientRect(hWnd, &client);
    Log("start %ld %ld", client.right - client.left, client.bottom - client.top);

    if (clipCursor && GetForegroundWindow() == hWnd)
        ClipToClient(hWnd);

    HMODULE overlay = LoadLibraryA(overlayLibrary);
    if (overlay == nullptr)
        Log("overlay_load_failed %lu", GetLastError());

    bool asyncDown[sizeof(WatchedKeys) / sizeof(*WatchedKeys)]{};
    bool stateDown[sizeof(WatchedKeys) / sizeof(*WatchedKeys)]{};
    std::string lastKeyboardState;
    RECT lastClip{};
    std::vector<BYTE> rawBuffer(16 * 1024);

    bool done = false;
    while (!done)
    {
        if (g_RawBuffer)
        {
            // Read the raw input like games do, before the message loop.
            for (;;)
            {
                UINT size = static_cast<UINT>(rawBuffer.size());
                const UINT count = GetRawInputBuffer(reinterpret_cast<PRAWINPUT>(rawBuffer.data()), &size, sizeof(RAWINPUTHEADER));
                if (count == 0 || count == (UINT)-1)
                    break;

                PRAWINPUT raw = reinterpret_cast<PRAWINPUT>(rawBuffer.data());
                for (UINT i = 0; i < count; ++i)
                {
                    LogRawKeyboard(*raw);
                    const ULONG_PTR next = reinterpret_cast<ULONG_PTR>(raw) + raw->header.dwSize;
                    raw = reinterpret_cast<PRAWINPUT>((next + sizeof(ULONG_PTR) - 1) & ~(ULONG_PTR)(sizeof(ULONG_PTR) - 1));
                }
            }
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;
            const char* category;
            if (const char* name = MouseMessageName(msg.message, &category))
                Log("queued %s %s", category, name);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done)
            break;

        for (size_t i = 0; i < sizeof(WatchedKeys) / sizeof(*WatchedKeys); ++i)
        {
            const bool async = (GetAsyncKeyState(WatchedKeys[i]) & 0x8000) != 0;
            if (async != asyncDown[i])
                Log("async %d %s", WatchedKeys[i], async ? "down" : "up");
            asyncDown[i] = async;

            const bool state = (GetKeyState(WatchedKeys[i]) & 0x8000) != 0;
            if (state != stateDown[i])
                Log("state %d %s", WatchedKeys[i], state ? "down" : "up");
            stateDown[i] = state;
        }

        // A buffer filled with a pattern tells whether GetKeyboardState wrote it.
        BYTE keyboardState[256];
        memset(keyboardState, 0xAA, sizeof(keyboardState));
        const BOOL keyboardStateResult = GetKeyboardState(keyboardState);
        bool written = false;
        for (BYTE value : keyboardState)
            written = written || value != 0xAA;
        std::string keyboardStateLine = std::string(keyboardStateResult ? "TRUE " : "FALSE ") + (written ? "written" : "untouched");
        for (int key : WatchedKeys)
        {
            if (written && (keyboardState[key] & 0x80))
                keyboardStateLine += " " + std::to_string(key);
        }
        if (keyboardStateLine != lastKeyboardState)
            Log("kbstate %s", keyboardStateLine.c_str());
        lastKeyboardState = keyboardStateLine;

        if (mouseLook && GetForegroundWindow() == hWnd)
        {
            const RECT rect = ClientRectOnScreen(hWnd);
            const POINT center{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
            POINT cursor;
            if (GetCursorPos(&cursor) && (cursor.x != center.x || cursor.y != center.y))
                Log("look %ld %ld", cursor.x - center.x, cursor.y - center.y);
            SetCursorPos(center.x, center.y);
        }

        if (clipCursor)
        {
            RECT clip;
            if (GetClipCursor(&clip) && memcmp(&clip, &lastClip, sizeof(clip)) != 0)
                Log("clip %ld %ld %ld %ld", clip.left, clip.top, clip.right, clip.bottom);
            GetClipCursor(&lastClip);
        }

        GetClientRect(hWnd, &client);
        glViewport(0, 0, client.right - client.left, client.bottom - client.top);
        glClearColor(0.45f, 0.55f, 0.60f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (hostilePixelState)
            SetHostilePixelState();
        if (leaveGLError)
            glEnable(0xFFFF); // GL_INVALID_ENUM, left in the error queue.

        SwapBuffers(g_DC);
        Sleep(10);
    }

    if (overlay != nullptr)
        FreeLibrary(overlay);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_RC);
    ReleaseDC(hWnd, g_DC);
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (g_Log != nullptr)
        fclose(g_Log);
    return 0;
}
