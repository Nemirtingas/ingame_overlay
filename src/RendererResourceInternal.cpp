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
	_AttachementChanged(false),
	_Data(nullptr),
	_Width(0),
	_Height(0)
{
}

RendererResourceInternal_t::~RendererResourceInternal_t()
{
	Unload();
}

bool RendererResourceInternal_t::_DoAutoLoad()
{
	if (_AutoLoad == ResourceAutoLoad_t::None || !CanBeLoaded())
		return false;

	_AttachementChanged = false;
	if (_AutoLoad == ResourceAutoLoad_t::Batch)
	{
		_RendererHook->AppendResourceToLoadBatch(this);
		return false;
	}

	_RendererResource = _RendererHook->CreateImageResource(_Data, _Width, _Height);
	return !_RendererResource.expired();
}

void RendererResourceInternal_t::Delete()
{
	delete this;
}

bool RendererResourceInternal_t::IsLoaded() const
{
	return !_RendererResource.expired();
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
		if (!_AttachementChanged)
			return true;

		Unload(false);
	}

	if (!CanBeLoaded())
		return false;

	_AttachementChanged = false;
	_RendererResource = _RendererHook->CreateImageResource(_Data, _Width, _Height);
	return !_RendererResource.expired();
}

bool RendererResourceInternal_t::Load(const void* data, uint32_t width, uint32_t height)
{
	if (IsLoaded())
		Unload(true);

	_Data = nullptr;
	_Width = width;
	_Height = height;
	_AttachementChanged = false;
	_RendererResource = _RendererHook->CreateImageResource(data, width, height);
	return !_RendererResource.expired();
}

uint64_t RendererResourceInternal_t::GetResourceId()
{
	if (!IsLoaded())
	{
		if (!_DoAutoLoad())
			return 0;
	}

	auto r = _RendererResource.lock();
	return r != nullptr ? *r : 0;
}

uint32_t RendererResourceInternal_t::Width() const
{
	return _Width;
}

uint32_t RendererResourceInternal_t::Height() const
{
	return _Height;
}

void RendererResourceInternal_t::AttachResource(const void* data, uint32_t width, uint32_t height)
{
	_AttachementChanged = true;
	_Data = data;
	_Width = width;
	_Height = height;
}

void RendererResourceInternal_t::ClearAttachedResource()
{
	_AttachementChanged = true;
	_Data = nullptr;

	if (_RendererResource.expired())
	{
		_Width = 0;
		_Height = 0;
	}
}

void RendererResourceInternal_t::Unload(bool clearAttachedResource)
{
	if (auto r = _RendererResource.lock())
		_RendererHook->ReleaseImageResource(_RendererResource);

	_RendererResource.reset();

	if (clearAttachedResource)
		ClearAttachedResource();

	_AttachementChanged = false;
}

}