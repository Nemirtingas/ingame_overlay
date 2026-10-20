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

#pragma once

#include "RendererHookInternal.h"

#include <set>
#include <vector>

struct ImDrawData;

namespace InGameOverlay {

class OpenGLHookCommon_t : public RendererHookInternal_t
{
protected:
    bool _Initialized;
    OverlayHookState _HookState;
    std::set<std::shared_ptr<RendererTexture_t>> _ImageResources;
    std::vector<RendererTextureLoadParameter_t> _ImageResourcesToLoad;
    std::vector<RendererTextureReleaseParameter_t> _ImageResourcesToRelease;
    void* _ImGuiFontAtlas;
    void* (*_GetCurrentContext)() = nullptr;

    OpenGLHookCommon_t();

    void _ResetRenderState(OverlayHookState state);
    void _RenderOverlayFrame();
    void _LoadResources();
    void _ReleaseResources();
    void _HandleScreenshot();

    bool _ContextIsCurrent() const;
    virtual bool _ImGuiBackendInit() = 0;
    virtual bool _ImGuiBackendNewFrame() = 0;
    virtual void _ImGuiBackendRenderDrawData(ImDrawData* drawData) = 0;
    virtual void _ImGuiBackendShutdown() = 0;
    virtual void _ResetWindowHook(OverlayHookState state) = 0;
    virtual void _ForgetWindow() = 0;

public:
    virtual std::weak_ptr<RendererTexture_t> AllocImageResource() override;
    virtual void LoadImageResource(RendererTextureLoadParameter_t& loadParameter) override;
    virtual void ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource) override;
};

}// namespace InGameOverlay
