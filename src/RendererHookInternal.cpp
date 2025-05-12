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

#include "RendererHookInternal.h"
#include "RendererResourceInternal.h"

namespace InGameOverlay {

RendererHookInternal_t::RendererHookInternal_t() :
	_ScreenshotCallback(nullptr),
	_ScreenshotCallbackUserParameter(nullptr),
	_TakeScreenshotType(ScreenshotType_t::None),
	_AutoLoad(ResourceAutoLoad_t::Batch),
	_BatchSize(10)
{
}

RendererHookInternal_t::~RendererHookInternal_t()
{
}

void RendererHookInternal_t::_LoadResources()
{
	if (_ResourcesToLoad.empty())
		return;

	auto batchSize = _ResourcesToLoad.size() > _BatchSize ? _BatchSize : _ResourcesToLoad.size();

	for (int i = 0; i < batchSize; ++i)
		_ResourcesToLoad[i]->LoadAttachedResource();

	_ResourcesToLoad.erase(_ResourcesToLoad.begin(), _ResourcesToLoad.begin() + batchSize);
}

ScreenshotType_t RendererHookInternal_t::_ScreenshotType()
{
	return _TakeScreenshotType;
}

void RendererHookInternal_t::_SendScreenshot(ScreenshotData_t* screenshot)
{
	_TakeScreenshotType = ScreenshotType_t::None;
	if (screenshot != nullptr && _ScreenshotCallback != nullptr)
		_ScreenshotCallback(screenshot, _ScreenshotCallbackUserParameter);
}

void RendererHookInternal_t::AppendResourceToLoadBatch(RendererResourceInternal_t* pResource)
{
	if (!pResource->CanBeLoaded())
		return;

	if (std::find(_ResourcesToLoad.begin(), _ResourcesToLoad.end(), pResource) == _ResourcesToLoad.end())
		_ResourcesToLoad.emplace_back(pResource);
}

void RendererHookInternal_t::SetScreenshotCallback(ScreenshotCallback_t callback, void* userParam)
{
	_ScreenshotCallback = callback;
	_ScreenshotCallbackUserParameter = userParam;
}

uint32_t RendererHookInternal_t::GetAutoLoadBatchSize()
{
	return _BatchSize;
}

void RendererHookInternal_t::SetAutoLoadBatchSize(uint32_t batchSize)
{
	_BatchSize = batchSize;
}

ResourceAutoLoad_t RendererHookInternal_t::GetResourceAutoLoad() const
{
	return _AutoLoad;
}

void RendererHookInternal_t::SetResourceAutoLoad(ResourceAutoLoad_t autoLoad)
{
	_AutoLoad = autoLoad;
}

void RendererHookInternal_t::TakeScreenshot(ScreenshotType_t type)
{
	_TakeScreenshotType = type;
}

RendererResource_t* RendererHookInternal_t::CreateResource()
{
	return new RendererResourceInternal_t(this, _AutoLoad);
}

RendererResource_t* RendererHookInternal_t::CreateAndLoadResource(const void* image_data, uint32_t width, uint32_t height, bool attach)
{
	auto pResource = CreateResource();

	bool failed = false;
	if (attach)
	{
		pResource->AttachResource(image_data, width, height);
		failed = !pResource->LoadAttachedResource();
	}
	else
	{
		failed = !pResource->Load(image_data, width, height);
	}

	if (failed)
	{
		pResource->Delete();
		pResource = nullptr;
	}

	return pResource;
}

}