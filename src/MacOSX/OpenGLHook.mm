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
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl2.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&OpenGLHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

OpenGLHook_t* OpenGLHook_t::_Instance = nullptr;

static bool ImGuiOpenGL3Init()
{
    return ImGui_ImplOpenGL3_Init(nullptr);
}

bool OpenGLHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_NSOpenGLContextFlushBufferMethod == nullptr && _CGLFlushDrawable == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGL: Rendering functions missing.");
            return false;
        }

        if (!NSViewHook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _NSViewHooked = true;

        if (_NSOpenGLContextFlushBufferMethod != nullptr)
        {
            _NSOpenGLContextflushBuffer = (decltype(_NSOpenGLContextflushBuffer))method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)_MyNSOpenGLContextFlushBuffer);
        }
        else if (_CGLFlushDrawable != nullptr)
        {
            BeginHook();
            TRY_HOOK_FUNCTION_OR_FAIL(CGLFlushDrawable);
            EndHook();
        }

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
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot();

        const bool has_textures = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
        ImFontAtlasUpdateNewFrame(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas), ImGui::GetFrameCount(), has_textures);

        ++_CurrentFrame;
        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();
        _ReleaseResources();

        ImGui::Render();

        _OpenGLDriver.ImGuiRenderDrawData(ImGui::GetDrawData());

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot();
    }
}

void OpenGLHook_t::_LoadResources()
{
    if (_ImageResourcesToLoad.empty())
        return;

    // Save old texture id
    GLint oldTex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);

    struct ValidTexture_t
    {
        std::shared_ptr<RendererTexture_t> Resource;
        const void* Data;
        uint32_t Width;
        uint32_t Height;
    };

    std::vector<ValidTexture_t> validResources;

    const auto loadParameterCount = _ImageResourcesToLoad.size() > _BatchSize ? _BatchSize : _ImageResourcesToLoad.size();

    for (size_t i = 0; i < loadParameterCount; ++i)
    {
        auto& param = _ImageResourcesToLoad[i];
        auto r = param.Resource.lock();
        if (!r) continue;

        validResources.push_back(ValidTexture_t{
            r,
            param.Data,
            param.Width,
            param.Height
        });
    }

    if (!validResources.empty())
    {
        for (size_t i = 0; i < validResources.size(); ++i)
        {
            auto& tex = validResources[i];

            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex.Resource->ImGuiTextureId));

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            // Upload pixels into texture
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.Width, tex.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.Data);

            tex.Resource->LoadStatus = RendererTextureStatus_e::Loaded;
        }
    }

    glBindTexture(GL_TEXTURE_2D, oldTex);

    _ImageResourcesToLoad.erase(_ImageResourcesToLoad.begin(),
        _ImageResourcesToLoad.begin() + loadParameterCount);
}

void OpenGLHook_t::_ReleaseResources()
{
    _ImageResourcesToRelease.clear();
}

void OpenGLHook_t::_HandleScreenshot()
{
    int viewport[8];
    int width, height;
    glGetIntegerv(GL_VIEWPORT, viewport); // viewport[2] = width, viewport[3] = height
    width = viewport[2];
    height = viewport[3];

    int bytesPerPixel = 4;

    std::vector<uint8_t> buffer(width * height * bytesPerPixel);

    GLboolean isDoubleBuffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &isDoubleBuffered);

    glReadBuffer(isDoubleBuffered ? GL_BACK : GL_FRONT);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

    std::vector<uint8_t> lineBuffer(width * bytesPerPixel);

    for (int i = 0; i < (height / 2); ++i)
    {
        uint8_t* topLine = buffer.data() + i * width * bytesPerPixel;
        uint8_t* bottomLine = buffer.data() + (height - i - 1) * width * bytesPerPixel;

        memcpy(lineBuffer.data(), topLine, width * bytesPerPixel);
        memcpy(topLine, bottomLine, width * bytesPerPixel);
        memcpy(bottomLine, lineBuffer.data(), width * bytesPerPixel);
    }

    ScreenshotCallbackParameter_t screenshot;
    screenshot.Width = width;
    screenshot.Height = height;
    screenshot.Pitch = bytesPerPixel * width;
    screenshot.Data = reinterpret_cast<void*>(buffer.data());
    screenshot.Format = InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;

    _SendScreenshot(&screenshot);   
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

void OpenGLHook_t::LoadFunctions(Method openGLFlushBufferMethod, decltype(::CGLFlushDrawable)* pfnCGLFlushDrawable)
{
    _NSOpenGLContextFlushBufferMethod = openGLFlushBufferMethod;
    _CGLFlushDrawable = pfnCGLFlushDrawable;
}

std::weak_ptr<RendererTexture_t> OpenGLHook_t::AllocImageResource()
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (glGetError() != GL_NO_ERROR)
        return std::weak_ptr<RendererTexture_t>{};

    auto ptr = std::shared_ptr<RendererTexture_t>(new RendererTexture_t(), [](RendererTexture_t* handle)
    {
        if (handle != nullptr)
        {
            auto resource = static_cast<GLuint>(handle->ImGuiTextureId);
            glDeleteTextures(1, &resource);
            delete handle;
        }
    });
    ptr->ImGuiTextureId = static_cast<uint64_t>(texture);

    _ImageResources.emplace(ptr);

    return ptr;
}

void OpenGLHook_t::LoadImageResource(RendererTextureLoadParameter_t& loadParameter)
{
    _ImageResourcesToLoad.emplace_back(loadParameter);
}

void OpenGLHook_t::ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
        {
            _ImageResources.erase(it);
            _ImageResourcesToRelease.emplace_back(RendererTextureReleaseParameter_t
            {
                std::move(ptr),
                _CurrentFrame
            });
        }
    }
}

}// namespace InGameOverlay