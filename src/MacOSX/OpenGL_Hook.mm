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
#include "NSView_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl2.h>

#include <glad/gl.h>

OpenGL_Hook* OpenGL_Hook::_inst = nullptr;

decltype(OpenGL_Hook::DLL_NAME) OpenGL_Hook::DLL_NAME;

bool OpenGL_Hook::StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_NSOpenGLContextFlushBufferMethod == nullptr && CGLFlushDrawable == nullptr)
        {
            SPDLOG_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        if (!NSView_Hook::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        SPDLOG_INFO("Hooked OpenGL");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;

        if (_NSOpenGLContextFlushBufferMethod != nullptr)
        {
            NSOpenGLContextflushBuffer = (decltype(NSOpenGLContextflushBuffer))method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)MyflushBuffer);
        }
        else if (CGLFlushDrawable != nullptr)
        {
            UnhookAll();
            BeginHook();
            HookFuncs(
                std::make_pair<void**, void*>((void**)&CGLFlushDrawable, (void*)&OpenGL_Hook::MyCGLFlushDrawable)
            );
            EndHook();
        }
    }
    return true;
}

void OpenGL_Hook::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        NSView_Hook::Inst()->HideAppInputs(hide);
    }
}

void OpenGL_Hook::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        NSView_Hook::Inst()->HideOverlayInputs(hide);
    }
}

bool OpenGL_Hook::IsStarted()
{
    return _Hooked;
}

void OpenGL_Hook::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(ingame_overlay::OverlayHookState::Removing);

        ImGui_ImplOpenGL2_Shutdown();
        //NSView_Hook::Inst()->_ResetRenderState();
        //ImGui::DestroyContext();

        _ImageResources.clear();

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGL_Hook::_PrepareForOverlay()
{
    if( !_Initialized )
    {
        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        ImGui_ImplOpenGL2_Init();

        _Initialized = true;
        OverlayHookReady(ingame_overlay::OverlayHookState::Ready);
    }

    if (NSView_Hook::Inst()->PrepareForOverlay() && ImGui_ImplOpenGL2_NewFrame())
    {
        ImGui::NewFrame();

        OverlayProc();

        ImGui::Render();

        GLint last_program;
        glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
        glUseProgram(0);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glUseProgram(last_program);
    }
}

CGLError OpenGL_Hook::MyflushBuffer(id self)
{
    OpenGL_Hook::Inst()->_PrepareForOverlay();
    return OpenGL_Hook::Inst()->NSOpenGLContextflushBuffer(self);
}

CGLError OpenGL_Hook::MyCGLFlushDrawable(CGLContextObj glDrawable)
{
    OpenGL_Hook::Inst()->_PrepareForOverlay();
    return OpenGL_Hook::Inst()->CGLFlushDrawable(glDrawable);
}

OpenGL_Hook::OpenGL_Hook():
    _Initialized(false),
    _Hooked(false),
    _ImGuiFontAtlas(nullptr),
    _NSOpenGLContextFlushBufferMethod(nullptr),
    NSOpenGLContextflushBuffer(nullptr),
    CGLFlushDrawable(nullptr)
{
    
}

OpenGL_Hook::~OpenGL_Hook()
{
    SPDLOG_INFO("OpenGL Hook removed");

    if (_Initialized)
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

std::string OpenGL_Hook::GetLibraryName() const
{
    return LibraryName;
}

void OpenGL_Hook::LoadFunctions(Method openGLFlushBufferMethod, decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable)
{
    _NSOpenGLContextFlushBufferMethod = openGLFlushBufferMethod;
    CGLFlushDrawable = pfnCGLFlushDrawable;
}

std::weak_ptr<uint64_t> OpenGL_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
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

void OpenGL_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}
