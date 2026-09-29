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

#include "OSDetector.h"

#include <filesystem>
#include <string_view>
#include <vector>

std::vector<std::filesystem::path> GetCurrentLoadedLibraries();
std::filesystem::path GetLibraryPath(void* libraryHandle);
void* GetLibraryHandle(std::filesystem::path const& libraryName);
void* GetLibrarySymbol(void* libraryHandle, std::string_view symbolName);
void* LoadLibraryFromPath(std::filesystem::path const& libraryPath);
void CloseLibrary(void* libraryHandle);

struct LibraryWrapper
{
	void* LibraryHandle;

	inline LibraryWrapper(std::filesystem::path const& libraryPath)
		: LibraryHandle(LoadLibraryFromPath(libraryPath))
	{
	}

	inline LibraryWrapper(void* libraryHandle)
		: LibraryHandle(libraryHandle)
	{

	}

	inline ~LibraryWrapper()
	{
		::CloseLibrary(LibraryHandle);
	}

	inline bool IsValid() const
	{
		return LibraryHandle != nullptr;
	}

	inline void* GetSymbol(std::string_view symbolName) const
	{
		return ::GetLibrarySymbol(LibraryHandle, symbolName);
	}
};