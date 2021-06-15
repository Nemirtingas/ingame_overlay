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

#include <library/library.h>

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__) \
 || defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
    #define LIBRARY_OS_WINDOWS
#elif defined(__linux__) || defined(linux)
    #define LIBRARY_OS_LINUX
#elif defined(__APPLE__)
    #define LIBRARY_OS_APPLE
#endif

#if defined(LIBRARY_OS_WINDOWS)
    #define WIN32_LEAN_AND_MEAN
    #define VC_EXTRALEAN
    #define NOMINMAX
    #include <Windows.h>

    std::string utf16_2_utf8(const std::wstring& wstr)
    {
        if (wstr.empty())
            return std::string();

        int utf8_size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string str(utf8_size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], utf8_size, nullptr, nullptr);
        return str;
    }

    std::wstring utf8_2_utf16(const std::string& str)
    {
        if (str.empty())
            return std::wstring();

        int utf16_size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
        std::wstring wstr(utf16_size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], utf16_size);
        return wstr;
    }

    constexpr char library_suffix[] = ".dll";

    void* Library::open_library(std::string const& library_name)
    {
        std::wstring wide(utf8_2_utf16(library_name));
        return LoadLibraryW(wide.c_str());
    }

    void Library::close_library(void* handle)
    {
        FreeLibrary((HMODULE)handle);
    }

    void* Library::get_symbol(void* handle, std::string const& symbol_name)
    {
        return GetProcAddress((HMODULE)handle, symbol_name.c_str());
    }

    void* Library::get_module_handle(std::string const& library_name)
    {
        std::wstring wide(utf8_2_utf16(library_name));
        return GetModuleHandleW(wide.c_str());
    }

    std::string Library::get_module_path(void* handle)
    {
        std::wstring wpath(1024, L'\0');

        DWORD size;
        while ((size = GetModuleFileNameW((HMODULE)handle, &wpath[0], wpath.length())) == wpath.length())
        {
            wpath.resize(wpath.length() * 2);
        }

        return utf16_2_utf8(wpath);
    }

#elif defined(LIBRARY_OS_LINUX) || defined(LIBRARY_OS_APPLE)
    #include <dlfcn.h>
    #include <cstring>

    #if defined(LIBRARY_OS_LINUX)
        constexpr char library_suffix[] = ".so";
    #else
        constexpr char library_suffix[] = ".dylib";
    #endif

    void* Library::open_library(std::string const& library_name)
    {
        return dlopen(library_name.c_str(), RTLD_NOW);
    }

    void Library::close_library(void* handle)
    {
        dlclose(handle);
    }

    void* Library::get_symbol(void* handle, std::string const& symbol_name)
    {
        return dlsym(handle, symbol_name.c_str());
    }

    #if defined(LIBRARY_OS_LINUX)
        #include <dirent.h> // to open directories
        #include <unistd.h>

        std::string Library::get_module_path(void* handle)
        {
            std::string res;

            std::string const self("/proc/self/map_files/");
            DIR* dir;
            struct dirent* dir_entry;

            dir = opendir(self.c_str());
            if (dir != nullptr)
            {
                std::string file_path;
                while ((dir_entry = readdir(dir)) != nullptr)
                {
                    file_path = (self + dir_entry->d_name);
                    if (dir_entry->d_type != DT_LNK)
                    {// Not a link
                        continue;
                    }

                    char* pos = dir_entry->d_name;
                    uintptr_t start = static_cast<uintptr_t>(strtoull(pos, &pos, 16));
                    if (start != 0)
                    {
                        ++pos;
                        uintptr_t end = static_cast<uintptr_t>(strtoull(pos, &pos, 16));
                        uintptr_t p = reinterpret_cast<uintptr_t>(handle);
                        if (end != 0 && (start <= p && p <= end))
                        {
                            size_t name_len = 32;
                            do
                            {
                                name_len *= 2;
                                res.resize(name_len);
                                name_len = readlink(file_path.c_str(), &res[0], res.length());
                            } while (name_len == res.length());
                            res.resize(name_len);
                            break;
                        }
                    }

                }

                closedir(dir);
            }

            return res;
        }

        void* Library::get_module_handle(std::string const& library_name)
        {
            std::string const self("/proc/self/map_files/");
            DIR* dir;
            struct dirent* dir_entry;
            std::string link_target;
            void* res = nullptr;

            dir = opendir(self.c_str());
            if (dir != nullptr)
            {
                std::string file_path;
                while ((dir_entry = readdir(dir)) != nullptr)
                {
                    file_path = (self + dir_entry->d_name);
                    if (dir_entry->d_type != DT_LNK)
                    {// Not a link
                        continue;
                    }

                    ssize_t name_len = 128;
                    do
                    {
                        name_len *= 2;
                        link_target.resize(name_len);
                        name_len = readlink(file_path.c_str(), &link_target[0], link_target.length());
                    } while (name_len == link_target.length());
                    link_target.resize(name_len);

                    auto pos = link_target.rfind('/');
                    if (pos != std::string::npos)
                    {
                        ++pos;
                        if (strncmp(link_target.c_str() + pos, library_name.c_str(), library_name.length()) == 0)
                        {
                            res = dlopen(link_target.c_str(), RTLD_NOW);
                            if (res != nullptr)
                            {// Like Windows' GetModuleHandle, we don't want to increment the ref counter
                                dlclose(res);
                            }
                            break;
                        }
                    }
                }

                closedir(dir);
            }

            return res;
        }
    #else
        #include <mach-o/dyld_images.h>

		std::string Library::get_module_path(void* handle)
        {
            task_dyld_info dyld_info;
            task_t t;
            pid_t pid = getpid();
            task_for_pid(mach_task_self(), pid, &t);
            mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
            
            if (task_info(t, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld_info), &count) == KERN_SUCCESS)
            {
                dyld_all_image_infos* dyld_img_infos = reinterpret_cast<dyld_all_image_infos*>(dyld_info.all_image_info_addr);
                for (int i = 0; i < dyld_img_infos->infoArrayCount; ++i)
                {
                    void* res = dlopen(dyld_img_infos->infoArray[i].imageFilePath, RTLD_NOW);
                    if (res != nullptr)
                    {
                        dlclose(res);
                        if(res == handle)
                            return std::string(dyld_img_infos->infoArray[i].imageFilePath);
                    }
                }
            }
            
            return std::string();
        }

        void* Library::get_module_handle(std::string const& library_name)
        {
            void* res = nullptr;

            task_dyld_info dyld_info;
            task_t t;
            pid_t pid = getpid();
            task_for_pid(mach_task_self(), pid, &t);
            mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

            if (task_info(t, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld_info), &count) == KERN_SUCCESS)
            {
                const char* pos;
                dyld_all_image_infos* dyld_img_infos = reinterpret_cast<dyld_all_image_infos*>(dyld_info.all_image_info_addr);
                for (int i = 0; i < dyld_img_infos->infoArrayCount; ++i)
                {
                    pos = strrchr(dyld_img_infos->infoArray[i].imageFilePath, '/');
                    if (pos != nullptr)
                    {
                        ++pos;
                        if (strncmp(pos, library_name.c_str(), library_name.length()) == 0)
                        {
                            res = dlopen(dyld_img_infos->infoArray[i].imageFilePath, RTLD_NOW);
                            if (res != nullptr)
                            {// Like Windows' GetModuleHandle, we don't want to increment the ref counter
                                dlclose(res);
                            }
                            break;
                        }
                    }
                }
            }

            return res;
        }
    #endif

#else
    #error "Unknown OS"
#endif

Library::Library()
{}

Library::Library(Library const& other)
{
    _handle = other._handle;
}

Library::Library(Library&& other) noexcept
{
    _handle = std::move(other._handle);
}

Library& Library::operator=(Library const& other)
{
    if (this != &other)
    {
        _handle = other._handle;
    }
    return *this;
}

Library& Library::operator=(Library && other) noexcept
{
    if (this != &other)
    {
        _handle = std::move(other._handle);
    }
    return *this;
}

bool Library::load_library(std::string const& library_name, bool append_extension)
{
    std::string lib_name = (append_extension ? library_name + library_suffix : library_name);

    void* lib = open_library(lib_name.c_str());

    if (lib == nullptr)
    {
        lib_name = "lib" + lib_name;
        lib = open_library(lib_name.c_str());
        if (lib == nullptr)
        {
            return false;
        }
    }

    _handle.reset(lib, close_library);
    return true;
}
