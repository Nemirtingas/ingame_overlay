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

#include <string>
#include <memory>
#include <functional>

class Library
{
    std::shared_ptr<void> _handle;

public:
    Library();
    Library(Library const&);
    Library(Library&&) noexcept;
    Library& operator=(Library const&);
    Library& operator=(Library&&) noexcept;

    bool load_library(const char* library_name, bool append_extension = true);

    template<typename T>
    inline std::function<T> get_func(const char* func_name)
    {
        if (_handle == nullptr)
            return nullptr;

        return std::function<T>(reinterpret_cast<T*>(get_symbol(_handle.get(), func_name)));
    }

	template<typename T>
    inline T* get_symbol(const char* symbol_name)
    {
        if (_handle == nullptr)
            return nullptr;

        return reinterpret_cast<T*>(get_symbol(_handle.get(), symbol_name));
    }

    inline bool load_library(std::string const& library_name, bool append_extension = true)
    {
        return load_library(library_name.c_str(), append_extension);
    }

    template<typename T>
    inline std::function<T> get_func(std::string const& func_name)
    {
        return get_func<T>(func_name.c_str());
    }

    template<typename T>
    inline T* get_symbol(std::string const& symbol_name)
    {
        return get_symbol<T>(symbol_name.c_str());
    }

    inline std::string get_module_path()
    {
        return get_module_path(_handle.get());
    }

    inline bool is_loaded() const
    {
        return _handle != nullptr;
    }

    // Triies to load the library, I suggest that you use a Library instance instead
    static void* open_library(const char* library_name);
    // Will decrease the OS' ref counter on the library, use it to close a handle opened by open_library.
    // A Library instance will automatically call this in the destructor
    static void  close_library(void* handle);
    // Will try to retrieve a symbol address from the library handle
    static void* get_symbol(void* handle, const char* symbol_name);
    // Get a pointer to the library, if it is not loaded, will return nullptr. This doesn't increment the OS' internal ref counter
    static void* get_module_handle(const char* library_name);
    // Get the library path of a module handle
    static std::string get_module_path(void* handle);
};
