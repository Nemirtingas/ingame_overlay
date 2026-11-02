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

#include "OpenGLHook.h"
#include "../WindowHook.h"
#include "../Library.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&OpenGLHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

OpenGLHook_t* OpenGLHook_t::_Instance = nullptr;

bool OpenGLHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        LibraryWrapper libOpenGL(LibraryPath);
        _GetCurrentContext = libOpenGL.IsValid() ? reinterpret_cast<void* (*)()>(libOpenGL.GetSymbol("wglGetCurrentContext")) : nullptr;
        if (_GetCurrentContext == nullptr)
        {
            INGAMEOVERLAY_ERROR("Failed to hook OpenGL: wglGetCurrentContext missing.");
            return false;
        }

        if (_WGLSwapBuffers == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        if (!WindowHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _WindowsHooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION_OR_FAIL(WGLSwapBuffers);
        EndHook();

        INGAMEOVERLAY_INFO("Hooked OpenGL");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void OpenGLHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        WindowHook_t::Inst()->HideAppInputs(hide);
    }
}

void OpenGLHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        WindowHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool OpenGLHook_t::IsStarted()
{
    return _Hooked;
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLHook_t::_PrepareForOverlay(HDC hDC)
{
    HWND hWnd = WindowFromDC(hDC);

    // Release the overlay when its window is gone or changed; it is rebuilt for the next valid window.
    if (hWnd == nullptr || hWnd != _LastWindow)
        _ResetRenderState(OverlayHookState::Removing);

    if (hWnd == nullptr)
        return;

    if (!_Initialized)
    {
        // The renderer can be obtained with GetRenderer, without the detector loading glad.
        if (gladLoaderLoadGL() < GLAD_MAKE_VERSION(3, 1))
        {
            INGAMEOVERLAY_WARN("Failed to load OpenGL functions or OpenGL version is lower than 3.1.");
            return;
        }

        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        if (!WindowHook_t::Inst()->SetInitialWindowSize(NativeWindow_t{ hWnd }))
            return;

        _ImGuiBackendInit();

        _LastWindow = hWnd;


        _Initialized = true;
        _ResetRenderState(OverlayHookState::Ready);
    }

    if (_ImGuiBackendNewFrame() && WindowHook_t::Inst()->PrepareForOverlay(NativeWindow_t{ hWnd }))
    {
        _RenderOverlayFrame();
    }
}

BOOL WINAPI OpenGLHook_t::_MyWGLSwapBuffers(HDC hDC)
{
    auto inst = OpenGLHook_t::Inst();
    inst->_PrepareForOverlay(hDC);
    return inst->_WGLSwapBuffers(hDC);
}

OpenGLHook_t::OpenGLHook_t():
    _Hooked(false),
    _WindowsHooked(false),
    _LastWindow(nullptr),
    _WGLSwapBuffers(nullptr)
{
}

OpenGLHook_t::~OpenGLHook_t()
{
    INGAMEOVERLAY_INFO("OpenGL Hook removed");

    // The context may already be gone on the teardown thread; do not issue GL calls.
    _ResetRenderState(OverlayHookState::Removing);

    if (_WindowsHooked)
        delete WindowHook_t::Inst();

    _Instance->UnhookAll();
    _Instance = nullptr;
}

OpenGLHook_t* OpenGLHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new OpenGLHook_t;

    return _Instance;
}

const char* OpenGLHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t OpenGLHook_t::GetRendererHookType() const
{
    return RendererHookType_t::OpenGL;
}

void OpenGLHook_t::LoadFunctions(WGLSwapBuffers_t pfnwglSwapBuffers)
{
    _WGLSwapBuffers = pfnwglSwapBuffers;
}

void OpenGLHook_t::SetLibraryPath(std::filesystem::path const& libraryPath)
{
    LibraryPath = libraryPath;
    LibraryName = LibraryPath.string();
}

bool OpenGLHook_t::_ImGuiBackendInit()
{
    return ImGui_ImplOpenGL3_Init();
}

bool OpenGLHook_t::_ImGuiBackendNewFrame()
{
    return ImGui_ImplOpenGL3_NewFrame();
}

void OpenGLHook_t::_ImGuiBackendRenderDrawData(ImDrawData* drawData)
{
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
}

void OpenGLHook_t::_ImGuiBackendShutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
}

void OpenGLHook_t::_ResetWindowHook(OverlayHookState state)
{
    WindowHook_t::Inst()->ResetRenderState(state);
}

void OpenGLHook_t::_ForgetWindow()
{
    _LastWindow = nullptr;
}

}// namespace InGameOverlay