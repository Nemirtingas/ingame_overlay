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

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

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

#include <dlfcn.h>
#include <link.h>

#if defined(INGAMEOVERLAY_OS_LINUX)

std::vector<std::filesystem::path> GetCurrentLoadedLibraries()
{
    const std::filesystem::path directory{ "/proc/self/map_files" };

    std::vector<std::filesystem::path> paths;
    std::unordered_set<std::filesystem::path> found;

    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (ec)
            break;

        std::error_code linkEc;
        if (!entry.is_symlink(linkEc) || linkEc)
            continue;

        const auto path = std::filesystem::canonical(entry.path(), linkEc);

        if (linkEc)
            continue;

        if (found.emplace(path).second)
            paths.emplace_back(path);
    }

    return paths;
}

std::filesystem::path GetLibraryPath(void* libraryHandle)
{
    if (libraryHandle == nullptr)
        return {};

    link_map* map = nullptr;

    if (::dlinfo(libraryHandle, RTLD_DI_LINKMAP, &map) != 0 || map == nullptr)
        return {};

    if (map->l_name == nullptr || map->l_name[0] == '\0')
        return {};

    std::error_code ec;
    const auto path = std::filesystem::canonical(map->l_name, ec);

    return ec ? std::filesystem::path(map->l_name) : path;
}

void* GetLibraryHandle(std::filesystem::path const& libraryName)
{
    if (libraryName.empty())
        return nullptr;

    const auto name = libraryName.filename().native();

    for (auto const& modulePath : GetCurrentLoadedLibraries())
    {
        const auto moduleName = modulePath.filename().native();

        if (moduleName.size() < name.size())
            continue;

        if (!std::equal(name.begin(), name.end(), moduleName.begin()))
            continue;

        if (moduleName.size() > name.size() && moduleName[name.size()] != '.')
            continue;

        void* handle = ::dlopen(modulePath.string().c_str(), RTLD_NOW);
        if (handle != nullptr)
        {
            ::dlclose(handle);
            return handle;
        }
    }

    return nullptr;
}

#elif defined(INGAMEOVERLAY_OS_APPLE)

#include <mach-o/dyld.h>

std::vector<std::filesystem::path> GetCurrentLoadedLibraries()
{
    std::vector<std::filesystem::path> paths;

    const uint32_t imageCount = ::_dyld_image_count();
    paths.reserve(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const char* imagePath = ::_dyld_get_image_name(i);
        if (imagePath == nullptr)
            continue;

        std::error_code ec;
        const auto path = std::filesystem::canonical(imagePath, ec);

        if (!ec)
            paths.emplace_back(path);
    }

    return paths;
}

std::filesystem::path GetLibraryPath(void* libraryHandle)
{
    if (libraryHandle == nullptr)
        return {};

    const uint32_t imageCount = ::_dyld_image_count();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const char* imagePath = ::_dyld_get_image_name(i);
        if (imagePath == nullptr)
            continue;

        void* handle = ::dlopen(imagePath, RTLD_LAZY | RTLD_NOLOAD);
        if (handle == nullptr)
            continue;

        const bool found = handle == libraryHandle;
        ::dlclose(handle);

        if (!found)
            continue;

        std::error_code ec;
        const auto path = std::filesystem::canonical(imagePath, ec);

        return ec ? std::filesystem::path(imagePath) : path;
    }

    return {};
}

void* GetLibraryHandle(std::filesystem::path const& libraryName)
{
    if (libraryName.empty())
        return nullptr;

    const auto name = libraryName.filename().native();

    const uint32_t imageCount = ::_dyld_image_count();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const char* imagePath = ::_dyld_get_image_name(i);
        if (imagePath == nullptr)
            continue;

        const std::filesystem::path modulePath(imagePath);
        const auto moduleName = modulePath.filename().native();

        if (moduleName.size() < name.size())
            continue;

        if (!std::equal(name.begin(), name.end(), moduleName.begin()))
            continue;

        if (moduleName.size() > name.size() && moduleName[name.size()] != '.')
            continue;

        void* handle = ::dlopen(imagePath, RTLD_NOW);
        if (handle != nullptr)
        {
            // Like Windows' GetModuleHandle, don't increment the ref counter.
            ::dlclose(handle);
            return handle;
        }
    }

    return nullptr;
}

#endif

void* GetLibrarySymbol(void* libraryHandle, std::string_view symbolName)
{
    if (libraryHandle == nullptr || symbolName.empty())
        return nullptr;

    if (symbolName.back() == '\0')
        return ::dlsym(libraryHandle, symbolName.data());

    const std::string name(symbolName);
    return ::dlsym(libraryHandle, name.c_str());
}

void* LoadLibraryFromPath(std::filesystem::path const& libraryPath)
{
    if (libraryPath.empty())
        return nullptr;

    return ::dlopen(libraryPath.string().c_str(), RTLD_NOW);
}

void CloseLibrary(void* libraryHandle)
{
    if (libraryHandle != nullptr)
        dlclose(libraryHandle);
}

#endif