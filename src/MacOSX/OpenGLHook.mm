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
#include "NSViewHook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl2.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&OpenGLHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

OpenGLHook_t* OpenGLHook_t::_Instance = nullptr;

static bool ImGuiOpenGL3Init()
{
    return ImGui_ImplOpenGL3_Init(nullptr);
}

bool OpenGLHook_t::StartHook(std::function<void()> key_combination_callback, std::set<InGameOverlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        if (_NSOpenGLContextFlushBufferMethod == nullptr && _CGLFlushDrawable == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        if (!NSViewHook_t::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        _NSViewHooked = true;

        if (_NSOpenGLContextFlushBufferMethod != nullptr)
        {
            _NSOpenGLContextflushBuffer = (decltype(_NSOpenGLContextflushBuffer))method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)_MyNSOpenGLContextFlushBuffer);
        }
        else if (_CGLFlushDrawable != nullptr)
        {
            BeginHook();
            TRY_HOOK_FUNCTION(CGLFlushDrawable);
            EndHook();
        }

        INGAMEOVERLAY_INFO("Hooked OpenGL");
        _Hooked = true;
        _ImGuiFontAtlas = imgui_font_atlas;
    }
    return true;
}

void OpenGLHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        NSViewHook_t::Inst()->HideAppInputs(hide);
    }
}

void OpenGLHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        NSViewHook_t::Inst()->HideOverlayInputs(hide);
    }
}

bool OpenGLHook_t::IsStarted()
{
    return _Hooked;
}

void OpenGLHook_t::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(InGameOverlay::OverlayHookState::Removing);

        _OpenGLDriver.ImGuiShutdown();
        //NSViewHook_t::Inst()->_ResetRenderState();
        //ImGui::DestroyContext();

        _ImageResources.clear();

        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLHook_t::_PrepareForOverlay()
{
    if( !_Initialized )
    {
        auto openGLVersion = gladLoaderLoadGL();
        if (openGLVersion < GLAD_MAKE_VERSION(2, 0))
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGL: Version is too low: {}.{}", GLAD_VERSION_MAJOR(openGLVersion), GLAD_VERSION_MINOR(openGLVersion));
            return;
        }

        if (openGLVersion >= GLAD_MAKE_VERSION(3, 0))
        {
            _OpenGLDriver.ImGuiInit = ImGuiOpenGL3Init;
            _OpenGLDriver.ImGuiNewFrame = ImGui_ImplOpenGL3_NewFrame;
            _OpenGLDriver.ImGuiRenderDrawData = ImGui_ImplOpenGL3_RenderDrawData;
            _OpenGLDriver.ImGuiShutdown = ImGui_ImplOpenGL3_Shutdown;
        }
        else
        {
            _OpenGLDriver.ImGuiInit = ImGui_ImplOpenGL2_Init;
            _OpenGLDriver.ImGuiNewFrame = ImGui_ImplOpenGL2_NewFrame;
            _OpenGLDriver.ImGuiRenderDrawData = ImGui_ImplOpenGL2_RenderDrawData;
            _OpenGLDriver.ImGuiShutdown = ImGui_ImplOpenGL2_Shutdown;
        }

        if(ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        _OpenGLDriver.ImGuiInit();

        _Initialized = true;
        OverlayHookReady(InGameOverlay::OverlayHookState::Ready);
    }

    if (_OpenGLDriver.ImGuiNewFrame() && NSViewHook_t::Inst()->PrepareForOverlay())
    {
        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();

        ImGui::Render();

        _OpenGLDriver.ImGuiRenderDrawData(ImGui::GetDrawData());
    }
}

CGLError OpenGLHook_t::_MyNSOpenGLContextFlushBuffer(id self)
{
    OpenGLHook_t::Inst()->_PrepareForOverlay();
    return OpenGLHook_t::Inst()->_NSOpenGLContextflushBuffer(self);
}

CGLError OpenGLHook_t::_MyCGLFlushDrawable(CGLContextObj glDrawable)
{
    OpenGLHook_t::Inst()->_PrepareForOverlay();
    return OpenGLHook_t::Inst()->_CGLFlushDrawable(glDrawable);
}

OpenGLHook_t::OpenGLHook_t():
    _Hooked(false),
    _Initialized(false),
    _ImGuiFontAtlas(nullptr),
    _NSOpenGLContextFlushBufferMethod(nullptr),
    _NSOpenGLContextflushBuffer(nullptr),
    _CGLFlushDrawable(nullptr)
{
    
}

OpenGLHook_t::~OpenGLHook_t()
{
    INGAMEOVERLAY_INFO("OpenGL Hook removed");

    if (_NSViewHooked)
        delete NSViewHook_t::Inst();

    if (_Initialized)
    {
        _OpenGLDriver.ImGuiShutdown();
        ImGui::DestroyContext();
    }

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

void OpenGLHook_t::LoadFunctions(Method openGLFlushBufferMethod, decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable)
{
    _NSOpenGLContextFlushBufferMethod = openGLFlushBufferMethod;
    _CGLFlushDrawable = pfnCGLFlushDrawable;
}

std::weak_ptr<uint64_t> OpenGLHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
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

void OpenGLHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
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