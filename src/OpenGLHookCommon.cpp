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
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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

std::weak_ptr<RendererTexture_t> OpenGLHookCommon_t::AllocImageResource()
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (glGetError() != GL_NO_ERROR)
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

    _LoadResources();
    _ReleaseResources();

    ImGui::Render();

    _ImGuiBackendRenderDrawData(ImGui::GetDrawData());

    if (screenshotType == ScreenshotType_t::AfterOverlay)
        _HandleScreenshot();
}

}// namespace InGameOverlay
