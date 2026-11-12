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

#include <glad/gl.h>

#include "OpenGLXHook.h"
#include "../WindowHook.h"
#include "../Library.h"

#undef Status

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <xcb/xcb.h>

#include <cstdlib>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&OpenGLXHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

OpenGLXHook_t* OpenGLXHook_t::_Instance = nullptr;

bool OpenGLXHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        LibraryWrapper libOpenGL(LibraryPath);
        _GetCurrentContext = libOpenGL.IsValid() ? reinterpret_cast<void* (*)()>(libOpenGL.GetSymbol("glXGetCurrentContext")) : nullptr;
        if (_GetCurrentContext == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to hook OpenGLX: glXGetCurrentContext missing.");
            return false;
        }
        _GLXQueryDrawable = reinterpret_cast<decltype(_GLXQueryDrawable)>(libOpenGL.GetSymbol("glXQueryDrawable"));

        if (_GLXSwapBuffers == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGLX: Rendering functions missing.");
            return false;
        }

        if (!WindowHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _X11Hooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION_OR_FAIL(GLXSwapBuffers);
        EndHook();

        INGAMEOVERLAY_INFO("Hooked OpenGLX");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void OpenGLXHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
        WindowHook_t::Inst()->HideAppInputs(hide);
}

void OpenGLXHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
        WindowHook_t::Inst()->HideOverlayInputs(hide);
}

bool OpenGLXHook_t::IsStarted()
{
    return _Hooked;
}

// Requests go through a private xcb connection: an error is returned with the reply, where Xlib would call
// the application's error handler, which exits by default.
static bool GetXWindowSize(xcb_connection_t* connection, Window window, uint32_t& width, uint32_t& height)
{
    xcb_generic_error_t* error = nullptr;
    xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, static_cast<xcb_drawable_t>(window)), &error);
    free(error);
    if (reply == nullptr)
        return false;

    width = reply->width;
    height = reply->height;
    free(reply);
    return true;
}

static bool IsXWindow(xcb_connection_t* connection, XID id)
{
    xcb_generic_error_t* error = nullptr;
    xcb_get_window_attributes_reply_t* reply = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, static_cast<xcb_window_t>(id)), &error);
    const bool isWindow = reply != nullptr;
    free(error);
    free(reply);
    return isWindow;
}

Window OpenGLXHook_t::_GetDrawableWindow(Display* display, GLXDrawable drawable)
{
    auto it = _DrawableWindows.find(drawable);
    if (it != _DrawableWindows.end())
        return it->second;

    // Searching walks every X window: not more than once per second for a drawable that isn't resolved.
    const auto now = std::chrono::steady_clock::now();
    if (now < _NextWindowSearch)
        return None;

    _NextWindowSearch = now + std::chrono::seconds(1);

    xcb_connection_t* connection = xcb_connect(DisplayString(display), nullptr);
    if (xcb_connection_has_error(connection))
    {
        xcb_disconnect(connection);
        // Can't check it, use it as a window like before.
        _DrawableWindows.emplace(drawable, static_cast<Window>(drawable));
        return static_cast<Window>(drawable);
    }

    Window window = None;
    if (IsXWindow(connection, drawable))
    {
        window = static_cast<Window>(drawable);
    }
    else
    {
        // A GLXWindow: find the application's window of the same size.
        unsigned int drawableWidth = 0;
        unsigned int drawableHeight = 0;
        if (_GLXQueryDrawable != nullptr)
        {
            _GLXQueryDrawable(display, drawable, GLX_WIDTH, &drawableWidth);
            _GLXQueryDrawable(display, drawable, GLX_HEIGHT, &drawableHeight);
        }

        for (auto const& applicationWindow : WindowHook_t::Inst()->FindApplicationWindows())
        {
            uint32_t width = 0;
            uint32_t height = 0;
            if (!GetXWindowSize(connection, applicationWindow.WindowHandle, width, height))
                continue;

            if (window == None)
                window = applicationWindow.WindowHandle;

            if (width == drawableWidth && height == drawableHeight)
            {
                window = applicationWindow.WindowHandle;
                break;
            }
        }

        if (window == None)
            INGAMEOVERLAY_WARN("No X window found for the GLX drawable {}.", drawable);
    }

    xcb_disconnect(connection);

    if (window != None)
        _DrawableWindows.emplace(drawable, window);

    return window;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLXHook_t::_PrepareForOverlay(Display* display, GLXDrawable drawable)
{
    const Window window = _GetDrawableWindow(display, drawable);
    if (window == None)
        return;

    if( !_Initialized )
    {
        // The renderer can be obtained with GetRenderer, without the detector loading glad.
        if (gladLoaderLoadGL() < GLAD_MAKE_VERSION(3, 1))
        {
            INGAMEOVERLAY_WARN("Failed to load OpenGL functions or OpenGL version is lower than 3.1.");
            return;
        }

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        //int attributes[] = { //can't be const b/c X11 doesn't like it.  Not sure if that's intentional or just stupid.
        //    GLX_RGBA, //apparently nothing comes after this?
        //    GLX_RED_SIZE,    8,
        //    GLX_GREEN_SIZE,  8,
        //    GLX_BLUE_SIZE,   8,
        //    GLX_ALPHA_SIZE,  8,
        //    //Ideally, the size would be 32 (or at least 24), but I have actually seen
        //    //  this size (on a modern OS even).
        //    GLX_DEPTH_SIZE, 16,
        //    GLX_DOUBLEBUFFER, True,
        //    None
        //};
        //
        //XVisualInfo* visual_info = glXChooseVisual(_Display, DefaultScreen(_Display), attributes);
        //if (visual_info == nullptr)
        //    return;
        //
        //_Context = glXCreateContext(_Display, visual_info, nullptr, True);
        //if (_Context == nullptr)
        //    return;

        if (!WindowHook_t::Inst()->SetInitialWindowSize(NativeWindow_t{ display, window }))
            return;

        _ImGuiBackendInit();

        _Display = display;
        _Initialized = true;
        _ResetRenderState(OverlayHookState::Ready);
    }

    //auto oldContext = glXGetCurrentContext();

    //glXMakeCurrent(_Display, drawable, _Context);

    if (_ImGuiBackendNewFrame() && WindowHook_t::Inst()->PrepareForOverlay(NativeWindow_t{ display, window }))
    {
        _RenderOverlayFrame();
    }

    //glXMakeCurrent(_Display, drawable, oldContext);
}

void OpenGLXHook_t::_MyGLXSwapBuffers(Display* display, GLXDrawable drawable)
{
    OpenGLXHook_t::Inst()->_PrepareForOverlay(display, drawable);
    OpenGLXHook_t::Inst()->_GLXSwapBuffers(display, drawable);
}

OpenGLXHook_t::OpenGLXHook_t():
    _Hooked(false),
    _X11Hooked(false),
    _Display(nullptr),
    _GLXQueryDrawable(nullptr),
    _GLXSwapBuffers(nullptr)
{
    //_library = dlopen(DLL_NAME);
}

OpenGLXHook_t::~OpenGLXHook_t()
{
    INGAMEOVERLAY_INFO("OpenGLX Hook removed");

    // The context may already be gone on the teardown thread; do not issue GL calls.
    _ResetRenderState(OverlayHookState::Removing);

    if (_X11Hooked)
        delete WindowHook_t::Inst();

    //dlclose(_library);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

OpenGLXHook_t* OpenGLXHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new OpenGLXHook_t;

    return _Instance;
}

const char* OpenGLXHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t OpenGLXHook_t::GetRendererHookType() const
{
    return RendererHookType_t::OpenGL;
}

void OpenGLXHook_t::LoadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers)
{
    _GLXSwapBuffers = pfnglXSwapBuffers;
}

void OpenGLXHook_t::SetLibraryPath(std::filesystem::path const& libraryPath)
{
    LibraryPath = libraryPath;
    LibraryName = LibraryPath.string();
}

bool OpenGLXHook_t::_ImGuiBackendInit()
{
    return ImGui_ImplOpenGL3_Init();
}

bool OpenGLXHook_t::_ImGuiBackendNewFrame()
{
    return ImGui_ImplOpenGL3_NewFrame();
}

void OpenGLXHook_t::_ImGuiBackendRenderDrawData(ImDrawData* drawData)
{
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
}

void OpenGLXHook_t::_ImGuiBackendShutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
}

void OpenGLXHook_t::_ResetWindowHook(OverlayHookState state)
{
    WindowHook_t::Inst()->ResetRenderState(state);
}

void OpenGLXHook_t::_ForgetWindow()
{
    _Display = nullptr;
    _DrawableWindows.clear();
    _NextWindowSearch = {};
}

}// namespace InGameOverlay
