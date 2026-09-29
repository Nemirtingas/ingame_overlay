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

#include "Library.h"

#if defined(INGAMEOVERLAY_OS_WINDOWS)

#include <Windows.h>
#include <TlHelp32.h>

std::vector<std::filesystem::path> GetCurrentLoadedLibraries()
{
    std::vector<std::filesystem::path> paths;

    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ::GetCurrentProcessId());

    if (snapshot == INVALID_HANDLE_VALUE)
        return paths;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (::Module32FirstW(snapshot, &entry) != FALSE)
    {
        do
        {
            if (const auto path = ::GetLibraryPath(entry.hModule); !path.empty())
                paths.emplace_back(path);
        } while (::Module32NextW(snapshot, &entry) != FALSE);
    }

    ::CloseHandle(snapshot);
    return paths;
}

std::filesystem::path GetLibraryPath(void* libraryHandle)
{
    if (libraryHandle == nullptr)
        return {};

    constexpr std::size_t InitialSize = 1024;
    constexpr std::size_t MaxSize = 32 * 1024;

    std::wstring buffer(InitialSize, L'\0');

    while (buffer.size() <= MaxSize)
    {
        const DWORD size = ::GetModuleFileNameW(
            static_cast<HMODULE>(libraryHandle),
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        if (size == 0)
            return {};

        if (size < buffer.size())
        {
            buffer.resize(size);
            return std::filesystem::path(std::move(buffer));
        }

        buffer.resize(buffer.size() * 2);
    }

    return {};
}

void* GetLibraryHandle(std::filesystem::path const& libraryName)
{
	if (libraryName.empty())
		return nullptr;

	return GetModuleHandleW(libraryName.native().c_str());
}

void* GetLibrarySymbol(void* libraryHandle, std::string_view symbolName)
{
    if (libraryHandle == nullptr || symbolName.empty())
        return nullptr;

    if (symbolName.back() == '\0')
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(libraryHandle), symbolName.data()));

    const std::string name(symbolName);

    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(libraryHandle), name.c_str()));
}

void* LoadLibraryFromPath(std::filesystem::path const& libraryPath)
{
    if (libraryPath.empty())
        return nullptr;

    return static_cast<void*>(LoadLibraryW(libraryPath.native().c_str()));
}

void CloseLibrary(void* libraryHandle)
{
    if (libraryHandle != nullptr)
        FreeLibrary(static_cast<HMODULE>(libraryHandle));
}

#elif defined(INGAMEOVERLAY_OS_LINUX) || defined(INGAMEOVERLAY_OS_APPLE)

std::vector<std::filesystem::path> GetCurrentLoadedLibraries()
{
    return std::vector<std::filesystem::path>{};
}

std::filesystem::path GetLibraryPath(void* libraryHandle)
{
    return std::filesystem::path{};
}

void* GetLibraryHandle(std::filesystem::path const& libraryName)
{
	return nullptr;
}

void* GetLibrarySymbol(void* libraryHandle, std::string_view symbolName)
{
    return nullptr;
}

void* LoadLibraryFromPath(std::filesystem::path const& libraryPath)
{
    return nullptr;
}

void CloseLibrary(void* libraryHandle)
{
    if (libraryHandle != nullptr)
        dlclose(libraryHandle);
}

#endif