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

RendererResourceInternal_t::RendererResourceInternal_t(RendererHookInternal_t* rendererHook, ResourceAutoLoad_t autoLoad) noexcept :
	_RendererHook(rendererHook),
	_AutoLoad(autoLoad),
	_Data(nullptr)
{
}

RendererResourceInternal_t::~RendererResourceInternal_t()
{
	Unload();
}

bool RendererResourceInternal_t::_DoBatchLoad()
{
	_RendererHook->AppendResourceToLoadBatch(this);
	return false;
}

bool RendererResourceInternal_t::_DoAutoLoad()
{
	if (_AutoLoad == ResourceAutoLoad_t::None || !CanBeLoaded())
		return false;

	if (_AutoLoad == ResourceAutoLoad_t::Batch)
		return _DoBatchLoad();

	UnloadOldResource();
	return LoadAttachedResource();
}

void RendererResourceInternal_t::Delete()
{
	delete this;
}

bool RendererResourceInternal_t::IsLoaded() const
{
	return !_RendererResource.RendererResource.expired();
}

ResourceAutoLoad_t RendererResourceInternal_t::AutoLoad() const
{
	return _AutoLoad;
}

void RendererResourceInternal_t::SetAutoLoad(ResourceAutoLoad_t autoLoad)
{
	_AutoLoad = autoLoad;
}

bool RendererResourceInternal_t::CanBeLoaded() const
{
	return _Data != nullptr;
}

bool RendererResourceInternal_t::LoadAttachedResource()
{
	if (IsLoaded())
	{
		if (!AttachementChanged())
			return true;

		Unload(false);
	}

	if (!CanBeLoaded())
		return false;

	_RendererResource.RendererResource = _RendererHook->CreateImageResource(_Data, _RendererResource.Width, _RendererResource.Height);
	return IsLoaded();
}

bool RendererResourceInternal_t::Load(const void* data, uint32_t width, uint32_t height)
{
	Unload(true);

	_Data = nullptr;
	_RendererResource.Width = width;
	_RendererResource.Height = height;
	_OldRendererResource.Reset();
	_RendererResource.RendererResource = _RendererHook->CreateImageResource(data, width, height);
	return IsLoaded();
}

uint64_t RendererResourceInternal_t::GetResourceId()
{
	if (AttachementChanged() || !IsLoaded())
	{
		if (!_DoAutoLoad() && !AttachementChanged())
			return 0;
	}

	if (IsLoaded())
	{
		// When in batch autoload, the old resource might still be loaded, unload it now.
		if (AttachementChanged())
			UnloadOldResource();

		auto r = _RendererResource.RendererResource.lock();
		if (r != nullptr)
			return *r;
	}
	if (AttachementChanged())
	{
		auto r = _OldRendererResource.RendererResource.lock();
		if (r != nullptr)
			return *r;
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

	if (auto r = _RendererResource.RendererResource.lock())
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
	if (auto r = _OldRendererResource.RendererResource.lock())
		_RendererHook->ReleaseImageResource(_OldRendererResource.RendererResource);

	_OldRendererResource.Reset();
}

}