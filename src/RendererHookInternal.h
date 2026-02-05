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

#include <InGameOverlay/RendererHook.h>
#include "InternalIncludes.h"

#include <set>
#include <memory>
#include <algorithm>

namespace InGameOverlay {

enum class RendererTextureStatus_e
{
    NotLoaded,
    Loading,
    Loaded,
};

struct RendererTexture_t
{
    uint64_t ImGuiTextureId = 0;
    RendererTextureStatus_e LoadStatus = RendererTextureStatus_e::NotLoaded;
};

struct RendererTextureLoadParameter_t
{
    std::weak_ptr<RendererTexture_t> Resource;
    const void* Data;
    uint32_t Height;
    uint32_t Width;
};

struct RendererTextureReleaseParameter_t
{
    std::shared_ptr<RendererTexture_t> Resource;
    uint64_t ReleaseFrame;
};

class RendererResourceInternal_t;

class RendererHookInternal_t : public RendererHook_t
{
    ScreenshotCallback_t _ScreenshotCallback;
    void* _ScreenshotCallbackUserParameter;
    ScreenshotType_t _TakeScreenshotType;

protected:
    uint32_t _BatchSize;
    uint64_t _CurrentFrame;

    RendererHookInternal_t();
    virtual ~RendererHookInternal_t();

    ScreenshotType_t _ScreenshotType();

    void _SendScreenshot(ScreenshotCallbackParameter_t* screenshot);

public:
    virtual void SetScreenshotCallback(ScreenshotCallback_t callback, void* userParam);

    virtual uint32_t GetAutoLoadBatchSize();

    virtual void SetAutoLoadBatchSize(uint32_t batchSize);

    virtual RendererResource_t* CreateResource();

    virtual RendererResource_t* CreateAndAttachResource(const void* image_data, uint32_t width, uint32_t height);

    virtual void TakeScreenshot(ScreenshotType_t type);

    virtual std::weak_ptr<RendererTexture_t> AllocImageResource() = 0;

    virtual void LoadImageResource(RendererTextureLoadParameter_t& loadParameter) = 0;

    virtual void ReleaseImageResource(std::weak_ptr<RendererTexture_t> resource) = 0;
};

}