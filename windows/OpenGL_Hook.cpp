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

#include "OpenGL_Hook.h"
#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

OpenGL_Hook* OpenGL_Hook::_inst = nullptr;

bool OpenGL_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (wglSwapBuffers == nullptr)
        {
            SPDLOG_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->start_hook(key_combination_callback))
            return false;

        windows_hooked = true;

        SPDLOG_INFO("Hooked OpenGL");

        hooked = true;

        UnhookAll();
        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)wglSwapBuffers, &OpenGL_Hook::MywglSwapBuffers)
        );
        EndHook();
    }
    return true;
}

bool OpenGL_Hook::is_started()
{
    return hooked;
}

void OpenGL_Hook::resetRenderState()
{
    if (initialized)
    {
        overlay_hook_ready(false);

        ImGui_ImplOpenGL3_Shutdown();
        Windows_Hook::Inst()->resetRenderState();
        ImGui::DestroyContext();

        last_window = nullptr;
        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGL_Hook::prepareForOverlay(HDC hDC)
{
    HWND hWnd = WindowFromDC(hDC);

    if (hWnd != last_window)
        resetRenderState();

    if (!initialized)
    {
        ImGui::CreateContext();
        ImGui_ImplOpenGL3_Init();

        last_window = hWnd;
        initialized = true;
        overlay_hook_ready(true);
    }

    if (ImGui_ImplOpenGL3_NewFrame() && Windows_Hook::Inst()->prepareForOverlay(hWnd))
    {
        ImGui::NewFrame();

        overlay_proc();

        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

BOOL WINAPI OpenGL_Hook::MywglSwapBuffers(HDC hDC)
{
    auto inst = OpenGL_Hook::Inst();
    inst->prepareForOverlay(hDC);
    return inst->wglSwapBuffers(hDC);
}

OpenGL_Hook::OpenGL_Hook():
    hooked(false),
    windows_hooked(false),
    initialized(false),
    last_window(nullptr),
    wglSwapBuffers(nullptr)
{
}

OpenGL_Hook::~OpenGL_Hook()
{
    SPDLOG_INFO("OpenGL Hook removed");

    if (windows_hooked)
        delete Windows_Hook::Inst();

    if (initialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }

    _inst = nullptr;
}

OpenGL_Hook* OpenGL_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new OpenGL_Hook;

    return _inst;
}

const char* OpenGL_Hook::get_lib_name() const
{
    return library_name.c_str();
}

void OpenGL_Hook::loadFunctions(wglSwapBuffers_t pfnwglSwapBuffers)
{
    wglSwapBuffers = pfnwglSwapBuffers;
}

std::weak_ptr<uint64_t> OpenGL_Hook::CreateImageResource(std::shared_ptr<Image> source)
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source->width(), source->height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, source->get_raw_pointer());

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

void OpenGL_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = image_resources.find(ptr);
        if (it != image_resources.end())
            image_resources.erase(it);
    }
}