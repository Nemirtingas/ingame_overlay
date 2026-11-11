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

#include "OpenGLHookCommon.h"

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>

namespace InGameOverlay {

// The application's pixel transfer state applies to our uploads and readbacks: a bound pixel buffer turns
// our pointer into an offset into it, a row length, skip or alignment changes the layout, and a readback
// may then write past our buffer. Use the defaults for our transfers and restore the application's state.
class ScopedPixelTransferState_t
{
    static constexpr int ParameterCount = 4;

    const bool _Pack;
    GLint _Parameters[ParameterCount];
    GLint _PixelBuffer;
    GLint _ReadFramebuffer;
    GLint _ReadBuffer;

    const GLenum* _ParameterNames() const
    {
        static constexpr GLenum PackNames[ParameterCount] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS };
        static constexpr GLenum UnpackNames[ParameterCount] = { GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS };
        return _Pack ? PackNames : UnpackNames;
    }

public:
    // pack: for glReadPixels, which also reads the default framebuffer's back buffer.
    explicit ScopedPixelTransferState_t(bool pack):
        _Pack(pack),
        _Parameters{},
        _PixelBuffer(0),
        _ReadFramebuffer(0),
        _ReadBuffer(0)
    {
        const GLenum* names = _ParameterNames();
        for (int i = 0; i < ParameterCount; ++i)
        {
            glGetIntegerv(names[i], &_Parameters[i]);
            glPixelStorei(names[i], i == 0 ? 4 : 0);
        }

        if (GLAD_GL_VERSION_2_1)
        {
            glGetIntegerv(_Pack ? GL_PIXEL_PACK_BUFFER_BINDING : GL_PIXEL_UNPACK_BUFFER_BINDING, &_PixelBuffer);
            if (_PixelBuffer != 0)
                glBindBuffer(_Pack ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER, 0);
        }

        if (_Pack)
        {
            if (GLAD_GL_VERSION_3_0)
            {
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &_ReadFramebuffer);
                if (_ReadFramebuffer != 0)
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
            glGetIntegerv(GL_READ_BUFFER, &_ReadBuffer);
        }
    }

    ~ScopedPixelTransferState_t()
    {
        if (_Pack)
        {
            glReadBuffer(static_cast<GLenum>(_ReadBuffer));
            if (_ReadFramebuffer != 0)
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(_ReadFramebuffer));
        }

        if (_PixelBuffer != 0)
            glBindBuffer(_Pack ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(_PixelBuffer));

        const GLenum* names = _ParameterNames();
        for (int i = 0; i < ParameterCount; ++i)
            glPixelStorei(names[i], _Parameters[i]);
    }

    ScopedPixelTransferState_t(ScopedPixelTransferState_t const&) = delete;
    ScopedPixelTransferState_t& operator=(ScopedPixelTransferState_t const&) = delete;
};

OpenGLHookCommon_t::OpenGLHookCommon_t():
    _Initialized(false),
    _HookState(OverlayHookState::Removing),
    _ImGuiFontAtlas(nullptr)
{
}

void OpenGLHookCommon_t::_LoadResources()
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

            if (tex.Data == nullptr || tex.Width == 0 || tex.Height == 0 || tex.Resource->ImGuiTextureId == 0)
            {
                tex.Resource->LoadStatus = RendererTextureStatus_e::Failed;
                continue;
            }

            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex.Resource->ImGuiTextureId));

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            // Upload pixels into texture
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.Width, tex.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.Data);

            tex.Resource->LoadStatus = RendererTextureStatus_e::Loaded;
        }
    }

    glBindTexture(GL_TEXTURE_2D, oldTex);

    _ImageResourcesToLoad.erase(_ImageResourcesToLoad.begin(),
        _ImageResourcesToLoad.begin() + loadParameterCount);
}

void OpenGLHookCommon_t::_ReleaseResources()
{
    // Keep the resources released during this frame, the current draw data may still use them.
    for (auto it = _ImageResourcesToRelease.begin(); it != _ImageResourcesToRelease.end();)
    {
        if (it->ReleaseFrame < _CurrentFrame)
        {
            it = _ImageResourcesToRelease.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void OpenGLHookCommon_t::_HandleScreenshot()
{
    // The default framebuffer's size, as the platform backend measured the window. The viewport is the
    // one the application last used, which may be an offscreen pass of another size.
    ImGuiIO& io = ImGui::GetIO();
    int width = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int height = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (width <= 0 || height <= 0)
    {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        width = viewport[2];
        height = viewport[3];
    }

    if (width <= 0 || height <= 0)
    {
        _SendScreenshot(nullptr);
        return;
    }

    int bytesPerPixel = 4;

    std::vector<uint8_t> buffer(static_cast<size_t>(width) * height * bytesPerPixel);

    {
        ScopedPixelTransferState_t pixelTransferState(true);

        GLboolean isDoubleBuffered = GL_FALSE;
        glGetBooleanv(GL_DOUBLEBUFFER, &isDoubleBuffered);

        glReadBuffer(isDoubleBuffered ? GL_BACK : GL_FRONT);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    }

    std::vector<uint8_t> lineBuffer(width * bytesPerPixel);

    for (int i = 0; i < (height / 2); ++i)
    {
        uint8_t* topLine = buffer.data() + i * width * bytesPerPixel;
        uint8_t* bottomLine = buffer.data() + (height - i - 1) * width * bytesPerPixel;

        memcpy(lineBuffer.data(), topLine, width * bytesPerPixel);
        memcpy(topLine, bottomLine, width * bytesPerPixel);
        memcpy(bottomLine, lineBuffer.data(), width * bytesPerPixel);
    }

    ScreenshotCallbackParameter_t screenshot{};
    screenshot.Width = width;
    screenshot.Height = height;
    screenshot.Pitch = bytesPerPixel * width;
    screenshot.Data = reinterpret_cast<void*>(buffer.data());
    screenshot.Format = InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;

    _SendScreenshot(&screenshot);
}

std::weak_ptr<RendererTexture_t> OpenGLHookCommon_t::AllocImageResource()
{
    // glGetError would also return the errors the application left.
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0)
        return std::weak_ptr<RendererTexture_t>{};

    // Resources may outlive the hook, so the deleter captures the context getter, not this.
    auto getCurrentContext = _GetCurrentContext;
    auto ptr = std::shared_ptr<RendererTexture_t>(new RendererTexture_t(), [getCurrentContext](RendererTexture_t* handle)
    {
        if (handle != nullptr)
        {
            auto resource = static_cast<GLuint>(handle->ImGuiTextureId);
            if (getCurrentContext() != nullptr)
                glDeleteTextures(1, &resource);
            delete handle;
        }
    });
    ptr->ImGuiTextureId = static_cast<uint64_t>(texture);

    _ImageResources.emplace(ptr);

    return ptr;
}

void OpenGLHookCommon_t::LoadImageResource(RendererTextureLoadParameter_t& loadParameter)
{
    _ImageResourcesToLoad.emplace_back(loadParameter);
}

void OpenGLHookCommon_t::ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource)
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

bool OpenGLHookCommon_t::_ContextIsCurrent() const
{
    return _GetCurrentContext() != nullptr;
}

void OpenGLHookCommon_t::_ResetRenderState(OverlayHookState state)
{
    if (_HookState == state)
        return;

    OverlayHookReady(state);

    _HookState = state;

    switch (state)
    {
        case OverlayHookState::Reset:
            break;

        case OverlayHookState::Removing:
            // Without a current context the GL entry points cannot be reloaded or called.
            // The application may have already destroyed its context during teardown.
            if (_ContextIsCurrent())
                _ImGuiBackendShutdown();
            _ResetWindowHook(state);
            //ImGui::DestroyContext();

            _ImageResources.clear();
            _ImageResourcesToLoad.clear();
            _ImageResourcesToRelease.clear();

            _ForgetWindow();
            _Initialized = false;
    }
}

void OpenGLHookCommon_t::_RenderOverlayFrame()
{
    auto screenshotType = _ScreenshotType();
    if (screenshotType == ScreenshotType_t::BeforeOverlay)
        _HandleScreenshot();

    if (_ImGuiFontAtlas != nullptr)
    {
        const bool has_textures = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
        ImFontAtlasUpdateNewFrame(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas), ImGui::GetFrameCount(), has_textures);
    }

    ++_CurrentFrame;
    ImGui::NewFrame();

    OverlayProc();

    {
        // Our texture uploads and the backend's (it updates the font atlas while rendering).
        ScopedPixelTransferState_t pixelTransferState(false);

        _LoadResources();
        _ReleaseResources();

        ImGui::Render();

        _ImGuiBackendRenderDrawData(ImGui::GetDrawData());
    }

    if (screenshotType == ScreenshotType_t::AfterOverlay)
        _HandleScreenshot();
}

}// namespace InGameOverlay
