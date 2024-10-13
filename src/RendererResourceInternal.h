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

#include <memory>

#include "InternalIncludes.h"

namespace InGameOverlay {

class RendererHookInternal_t;

class RendererResourceInternal_t : public RendererResource_t
{
protected:
    RendererHookInternal_t* _RendererHook;

    bool _DoBatchLoad();
    bool _DoImmediateLoad();
    bool _DoAutoLoad();

public:
    std::weak_ptr<uint64_t> _OldRendererResource;
    std::weak_ptr<uint64_t> _RendererResource;
    ResourceAutoLoad_t _AutoLoad;
    const void* _Data;
    uint32_t _Width;
    uint32_t _Height;

    RendererResourceInternal_t(RendererHookInternal_t* rendererHook, ResourceAutoLoad_t autoLoad) noexcept;

    RendererResourceInternal_t(RendererResourceInternal_t &&) noexcept = default;
    RendererResourceInternal_t& operator=(RendererResourceInternal_t &&) noexcept = default;

    RendererResourceInternal_t(RendererResourceInternal_t const&) = delete;
    RendererResourceInternal_t& operator=(RendererResourceInternal_t const&) = delete;

    virtual ~RendererResourceInternal_t();

    virtual void Delete();

    virtual bool IsLoaded() const;

    virtual ResourceAutoLoad_t AutoLoad() const;

    virtual void SetAutoLoad(ResourceAutoLoad_t autoLoad);

    virtual bool CanBeLoaded() const;

    virtual bool LoadAttachedResource();

    virtual bool Load(const void* data, uint32_t width, uint32_t height);

    virtual uint64_t GetResourceId();

    virtual uint32_t Width() const;

    virtual uint32_t Height() const;

    virtual void AttachResource(const void* data, uint32_t width, uint32_t height);

    virtual void ClearAttachedResource();

    virtual void Unload(bool clearAttachedResource = true);

    bool AttachementChanged();

    void UnloadOldResource();
};

}