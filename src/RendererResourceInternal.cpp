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

#include "InternalIncludes.h"
#include "RendererHookInternal.h"
#include "RendererResourceInternal.h"

namespace InGameOverlay {

RendererResourceInternal_t::RendererResourceInternal_t(RendererHookInternal_t* rendererHook) noexcept :
    _RendererHook(rendererHook),
    _Data(nullptr)
{
}

RendererResourceInternal_t::~RendererResourceInternal_t()
{
    Unload();
}

void RendererResourceInternal_t::Delete()
{
    delete this;
}

bool RendererResourceInternal_t::IsLoaded() const
{
    return !_RendererResource.RendererResource.expired();
}

bool RendererResourceInternal_t::HasAttachedResource() const
{
    return _Data != nullptr;
}

uint64_t RendererResourceInternal_t::GetResourceId()
{
    if (HasAttachedResource())
    {
        auto r = _RendererResource.RendererResource.lock();
        if (r == nullptr)
        {
            _RendererResource.RendererResource = _RendererHook->AllocImageResource();
            r = _RendererResource.RendererResource.lock();
        }

        if (r != nullptr)
        {
            switch (r->LoadStatus)
            {
                case RendererTextureStatus_e::NotLoaded:
                {
                    RendererTextureLoadParameter_t loadParameter;
                    loadParameter.Resource = _RendererResource.RendererResource;
                    loadParameter.Data = _Data;
                    loadParameter.Height = _RendererResource.Height;
                    loadParameter.Width = _RendererResource.Width;
                    r->LoadStatus = RendererTextureStatus_e::Loading;
                    _RendererHook->LoadImageResource(loadParameter);
                }
                break;

                case RendererTextureStatus_e::Loading: break;
                case RendererTextureStatus_e::Loaded:
                    if (AttachementChanged())
                        UnloadOldResource();

                    return r->ImGuiTextureId;
            }
        }
    }
    if (AttachementChanged())
    {
        auto r = _OldRendererResource.RendererResource.lock();
        if (r != nullptr)
            return r->ImGuiTextureId;
    }

    return 0;
}

uint32_t RendererResourceInternal_t::Width() const
{
    return IsLoaded()
        ? _RendererResource.Width
        : _OldRendererResource.Width;
}

uint32_t RendererResourceInternal_t::Height() const
{
    return IsLoaded()
        ? _RendererResource.Height
        : _OldRendererResource.Height;
}

void RendererResourceInternal_t::AttachResource(const void* data, uint32_t width, uint32_t height)
{
    if (IsLoaded())
        _OldRendererResource = _RendererResource;

    _RendererResource.RendererResource.reset();
    _Data = data;
    _RendererResource.Width = width;
    _RendererResource.Height = height;
}

void RendererResourceInternal_t::ClearAttachedResource()
{
    _Data = nullptr;
}

void RendererResourceInternal_t::Unload(bool clearAttachedResource)
{
    UnloadOldResource();

    _RendererHook->ReleaseImageResource(_RendererResource.RendererResource);
    _RendererResource.Reset();

    if (clearAttachedResource)
        ClearAttachedResource();
}

bool RendererResourceInternal_t::AttachementChanged()
{
    return !_OldRendererResource.RendererResource.expired();
}

void RendererResourceInternal_t::UnloadOldResource()
{
    _RendererHook->ReleaseImageResource(_OldRendererResource.RendererResource);
    _OldRendererResource.Reset();
}

}