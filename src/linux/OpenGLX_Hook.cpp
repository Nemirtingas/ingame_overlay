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

#include "OpenGLX_Hook.h"
#include "X11_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

OpenGLX_Hook* OpenGLX_Hook::_inst = nullptr;

bool OpenGLX_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (glXSwapBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook OpenGLX: Rendering functions missing.");
            return false;
        }

        if (!X11_Hook::Inst()->start_hook(key_combination_callback))
            return false;

        x11_hooked = true;

        SPDLOG_INFO("Hooked OpenGLX");

        hooked = true;

        UnhookAll();
        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>((void**)&glXSwapBuffers, (void*)&OpenGLX_Hook::MyglXSwapBuffers)
        );
        EndHook();
    }
    return true;
}

bool OpenGLX_Hook::is_started()
{
    return hooked;
}

void OpenGLX_Hook::resetRenderState()
{
    if (initialized)
    {
        Renderer_Hook::overlay_hook_ready(false);

        ImGui_ImplOpenGL3_Shutdown();
        X11_Hook::Inst()->resetRenderState();
        ImGui::DestroyContext();

        glXDestroyContext(display, context);
        display = nullptr;
        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLX_Hook::prepareForOverlay(Display* display, GLXDrawable drawable)
{
    if( !initialized )
    {
        ImGui::CreateContext();
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
        //XVisualInfo* visual_info = glXChooseVisual(display, DefaultScreen(display), attributes);
        //if (visual_info == nullptr)
        //    return;
        //
        //context = glXCreateContext(display, visual_info, nullptr, True);
        //if (context == nullptr)
        //    return;

        this->display = display;

        initialized = true;
        overlay_hook_ready(true);
    }

    //auto oldContext = glXGetCurrentContext();

    //glXMakeCurrent(display, drawable, context);

    if (ImGui_ImplOpenGL3_NewFrame() && X11_Hook::Inst()->prepareForOverlay(display, (Window)drawable))
    {
        ImGui::NewFrame();

        overlay_proc();

        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    //glXMakeCurrent(display, drawable, oldContext);
}

void OpenGLX_Hook::MyglXSwapBuffers(Display* display, GLXDrawable drawable)
{
    OpenGLX_Hook::Inst()->prepareForOverlay(display, drawable);
    OpenGLX_Hook::Inst()->glXSwapBuffers(display, drawable);
}

OpenGLX_Hook::OpenGLX_Hook():
    initialized(false),
    hooked(false),
    x11_hooked(false),
    glXSwapBuffers(nullptr)
{
    //_library = dlopen(DLL_NAME);
}

OpenGLX_Hook::~OpenGLX_Hook()
{
    SPDLOG_INFO("OpenGLX Hook removed");

    if (x11_hooked)
        delete X11_Hook::Inst();

    if (initialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        glXDestroyContext(display, context);
    }

    //dlclose(_library);

    _inst = nullptr;
}

OpenGLX_Hook* OpenGLX_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new OpenGLX_Hook;

    return _inst;
}

std::string OpenGLX_Hook::GetLibraryName() const
{
    return LibraryName;
}

void OpenGLX_Hook::loadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers)
{
    glXSwapBuffers = pfnglXSwapBuffers;
}

std::weak_ptr<uint64_t> OpenGLX_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
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

    image_resources.emplace(ptr);
    return ptr;
}

void OpenGLX_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = image_resources.find(ptr);
        if (it != image_resources.end())
            image_resources.erase(it);
    }
}
