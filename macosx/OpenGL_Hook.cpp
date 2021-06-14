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

#include "OpenGL_Hook.h"
//#include "X11_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl2.h>

#include <glad/gl.h>

OpenGL_Hook* OpenGL_Hook::_inst = nullptr;

bool OpenGL_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (CGLFlushDrawable == nullptr)
        {
            SPDLOG_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        SPDLOG_ERROR("MacOS OpenGL hook is not supported.");

        // This hook works, but I don't know yet how to hook UI events.
        return false;
        //if (!X11_Hook::Inst()->start_hook(key_combination_callback))
        //    return false;

        SPDLOG_INFO("Hooked OpenGL");

        hooked = true;

        UnhookAll();
        BeginHook();
        HookFuncs(
                  std::make_pair<void**, void*>((void**)&CGLFlushDrawable, (void*)&OpenGL_Hook::MyCGLFlushDrawable)
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
        Renderer_Hook::overlay_hook_ready(false);

        ImGui_ImplOpenGL2_Shutdown();
        //X11_Hook::Inst()->resetRenderState();
        ImGui::DestroyContext();

        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGL_Hook::prepareForOverlay()
{
    if( !initialized )
    {
        ImGui::CreateContext();
        ImGui_ImplOpenGL2_Init();

        // Set dummy DisplaySize so Dear ImGui doesn't crash.
        ImGui::GetIO().DisplaySize = ImVec2{800,600};
        
        initialized = true;
        overlay_hook_ready(true);
    }

    if (ImGui_ImplOpenGL2_NewFrame())
    {
        ImGui::NewFrame();

        overlay_proc();

        ImGui::Render();

        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    }
}

int64_t OpenGL_Hook::MyCGLFlushDrawable(CGLDrawable_t *glDrawable)
{
    OpenGL_Hook::Inst()->prepareForOverlay();
    return OpenGL_Hook::Inst()->CGLFlushDrawable(glDrawable);
}

OpenGL_Hook::OpenGL_Hook():
    initialized(false),
    hooked(false),
    CGLFlushDrawable(nullptr)
{
    
}

OpenGL_Hook::~OpenGL_Hook()
{
    SPDLOG_INFO("OpenGL Hook removed");

    if (initialized)
    {
        ImGui_ImplOpenGL2_Shutdown();
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
    return OpenGL_HookConsts::dll_name;
}

void OpenGL_Hook::loadFunctions(decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable)
{
    CGLFlushDrawable = pfnCGLFlushDrawable;
}

std::weak_ptr<uint64_t> OpenGL_Hook::CreateImageResource(std::shared_ptr<Image> source)
{
    return std::shared_ptr<uint64_t>(nullptr);
}

void OpenGL_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
}
