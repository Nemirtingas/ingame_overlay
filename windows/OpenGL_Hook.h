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

#include "../Renderer_Hook.h"

class OpenGL_Hook : public Renderer_Hook
{
public:
    static constexpr const char *DLL_NAME = "opengl32.dll";

    using wglSwapBuffers_t = BOOL(WINAPI*)(HDC);

private:
    static OpenGL_Hook* _inst;

    // Variables
    bool hooked;
    bool windows_hooked;
    bool initialized;
    HWND last_window;
    std::set<std::shared_ptr<uint64_t>> image_resources;

    // Functions
    OpenGL_Hook();

    void resetRenderState();
    void prepareForOverlay(HDC hDC);

    // Hook to render functions
    static BOOL WINAPI MywglSwapBuffers(HDC hDC);

    wglSwapBuffers_t wglSwapBuffers;

public:
    virtual ~OpenGL_Hook();

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback);
    virtual bool is_started();
    static OpenGL_Hook* Inst();
    virtual const char* get_lib_name() const;
    void loadFunctions(wglSwapBuffers_t pfnwglSwapBuffers);

    virtual std::weak_ptr<uint64_t> CreateImageResource(std::shared_ptr<Image> source);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
