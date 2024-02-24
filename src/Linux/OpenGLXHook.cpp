/*
 * Copyright (C) 2019-2020 Nemirtingas
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
#include "X11Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

namespace InGameOverlay {

OpenGLXHook_t* OpenGLXHook_t::_Instance = nullptr;

constexpr decltype(OpenGLXHook_t::DLL_NAME) OpenGLXHook_t::DLL_NAME;

bool OpenGLXHook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_GLXSwapBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook OpenGLX: Rendering functions missing.");
            return false;
        }

        if (!X11Hook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _X11Hooked = true;

        SPDLOG_INFO("Hooked OpenGLX");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        UnhookAll();
        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>((void**)&_GLXSwapBuffers, (void*)&OpenGLXHook_t::My_GLXSwapBuffers)
        );
        EndHook();
    }
    return true;
}

void OpenGLXHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        X11Hook_t::Inst()->HideAppInputs(hide);
    }
}

void OpenGLXHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        X11Hook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool OpenGLXHook_t::IsStarted()
{
    return _Hooked;
}

void OpenGLXHook_t::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(InGameOverlay::OverlayHookState::Removing);

        ImGui_ImplOpenGL3_Shutdown();
        X11Hook_t::Inst()->ResetRenderState();
        //ImGui::DestroyContext();

        _ImageResources.clear();

        glXDestroyContext(_Display, _Context);
        _Display = nullptr;
        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLXHook_t::_PrepareForOverlay(Display* display, GLXDrawable drawable)
{
    if( !_Initialized )
    {
        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplOpenGL3_Init();

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

        _Display = display;

        X11Hook_t::Inst()->SetInitialWindowSize(_Display, (Window)drawable);

        _Initialized = true;
        OverlayHookReady(InGameOverlay::OverlayHookState::Ready);
    }

    //auto oldContext = glXGetCurrentContext();

    //glXMakeCurrent(_Display, drawable, _Context);

    if (ImGui_ImplOpenGL3_NewFrame() && X11Hook_t::Inst()->PrepareForOverlay(_Display, (Window)drawable))
    {
        ImGui::NewFrame();

        OverlayProc();

        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    //glXMakeCurrent(_Display, drawable, oldContext);
}

void OpenGLXHook_t::My_GLXSwapBuffers(Display* display, GLXDrawable drawable)
{
    OpenGLXHook_t::Inst()->_PrepareForOverlay(display, drawable);
    OpenGLXHook_t::Inst()->_GLXSwapBuffers(display, drawable);
}

OpenGLXHook_t::OpenGLXHook_t():
    _Initialized(false),
    _Hooked(false),
    _X11Hooked(false),
    _ImGuiFontAtlas(nullptr),
    _GLXSwapBuffers(nullptr)
{
    //_library = dlopen(DLL_NAME);
}

OpenGLXHook_t::~OpenGLXHook_t()
{
    SPDLOG_INFO("OpenGLX Hook removed");

    if (_X11Hooked)
        delete X11Hook_t::Inst();

    if (_Initialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        glXDestroyContext(_Display, _Context);
    }

    //dlclose(_library);

    _Instance = nullptr;
}

OpenGLXHook_t* OpenGLXHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new OpenGLXHook_t;

    return _Instance;
}

std::string OpenGLXHook_t::GetLibraryName() const
{
    return LibraryName;
}

void OpenGLXHook_t::LoadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers)
{
    _GLXSwapBuffers = pfnglXSwapBuffers;
}

std::weak_ptr<uint64_t> OpenGLXHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    GLuint* texture = new GLuint(0);
    glGenTextures(1, texture);
    if (glGetError() != GL_NO_ERROR)
    {
        delete texture;
        return std::shared_ptr<uint64_t>(nullptr);
    }
    
    // Save old texture id
    GLint oldTex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);

    glBindTexture(GL_TEXTURE_2D, *texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    glBindTexture(GL_TEXTURE_2D, oldTex);

    auto ptr = std::shared_ptr<uint64_t>((uint64_t*)texture, [](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            GLuint* texture = (GLuint*)handle;
            glDeleteTextures(1, texture);
            delete texture;
        }
    });

    _ImageResources.emplace(ptr);
    return ptr;
}

void OpenGLXHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}

}// namespace InGameOverlay