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

namespace InGameOverlay {

class RendererResourceInternal_t;

class RendererHookInternal_t : public RendererHook_t
{
    ResourceAutoLoad_t _AutoLoad;
    uint32_t _BatchSize;
    std::vector<RendererResourceInternal_t*> _ResourcesToLoad;

protected:
    RendererHookInternal_t();
    virtual ~RendererHookInternal_t();

    void _LoadResources();

public:
    void AppendResourceToLoadBatch(RendererResourceInternal_t* pResource);

    virtual uint32_t GetAutoLoadBatchSize();

    virtual void SetAutoLoadBatchSize(uint32_t batchSize);

    virtual ResourceAutoLoad_t GetResourceAutoLoad() const;

    virtual void SetResourceAutoLoad(ResourceAutoLoad_t autoLoad = ResourceAutoLoad_t::Batch);

    virtual RendererResource_t* CreateResource();

    virtual RendererResource_t* CreateAndLoadResource(const void* image_data, uint32_t width, uint32_t height, bool attach);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height) = 0;

    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource) = 0;
};

}