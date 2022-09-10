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

#include <cassert>

#include <ingame_overlay/Renderer_Detector.h>

#include <System/Encoding.hpp>
#include <System/String.hpp>
#include <System/System.h>
#include <System/Library.h>
#include <System/ScopedLock.hpp>
#include <mini_detour/mini_detour.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#define RENDERERDETECTOR_OS_LINUX

#include "OpenGLX_Hook.h"

class Renderer_Detector
{
    static Renderer_Detector* instance;
public:
    static Renderer_Detector* Inst()
    {
        if (instance == nullptr)
        {
            instance = new Renderer_Detector;
        }
        return instance;
    }

    ~Renderer_Detector()
    {
        stop_detection();

        delete openglx_hook;
        //delete vulkan_hook;

        instance = nullptr;
    }

private:
    std::timed_mutex detector_mutex;
    std::mutex renderer_mutex;

    Base_Hook detection_hooks;
    ingame_overlay::Renderer_Hook* renderer_hook;

    bool detection_done;
    uint32_t detection_count;
    bool detection_cancelled;
    std::condition_variable stop_detection_cv;
    std::mutex stop_detection_mutex;

    decltype(::glXSwapBuffers)* glXSwapBuffers;

    bool openglx_hooked;
    //bool vulkan_hooked;

    OpenGLX_Hook* openglx_hook;
    //Vulkan_Hook* vulkan_hook;

    Renderer_Detector() :
        openglx_hooked(false),
        renderer_hook(nullptr),
        openglx_hook(nullptr),
        //vulkan_hook(nullptr),
        detection_done(false),
        detection_count(0),
        detection_cancelled(false)
    {}

    std::string FindPreferedModulePath(std::string const& name)
    {
        return name;
    }

    static void MyglXSwapBuffers(Display* dpy, GLXDrawable drawable)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->glXSwapBuffers(dpy, drawable);
        if (inst->detection_done)
            return;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
        {
            inst->detection_hooks.UnhookAll();
            inst->renderer_hook = static_cast<ingame_overlay::Renderer_Hook*>(Inst()->openglx_hook);
            inst->openglx_hook = nullptr;
            inst->detection_done = true;
        }
    }

    void HookglXSwapBuffers(decltype(::glXSwapBuffers)* _glXSwapBuffers)
    {
        glXSwapBuffers = _glXSwapBuffers;

        detection_hooks.BeginHook();
        detection_hooks.HookFunc(std::pair<void**, void*>{ (void**)&glXSwapBuffers, (void*)&MyglXSwapBuffers });
        detection_hooks.EndHook();
    }

    void hook_openglx(std::string const& library_path)
    {
        if (!openglx_hooked)
        {
            System::Library::Library libGLX;
            if (!libGLX.OpenLibrary(library_path, false))
            {
                SPDLOG_WARN("Failed to load {} to detect OpenGLX", library_path);
                return;
            }

            auto glXSwapBuffers = libGLX.GetSymbol<decltype(::glXSwapBuffers)>("glXSwapBuffers");
            if (glXSwapBuffers != nullptr)
            {
                SPDLOG_INFO("Hooked glXSwapBuffers to detect OpenGLX");

                openglx_hooked = true;

                openglx_hook = OpenGLX_Hook::Inst();
                openglx_hook->LibraryName = library_path;
                openglx_hook->LoadFunctions(glXSwapBuffers);

                HookglXSwapBuffers(glXSwapBuffers);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook glXSwapBuffers to detect OpenGLX");
            }
        }
    }

    bool EnterDetection()
    {
        return true;
    }

    void ExitDetection()
    {
        detection_done = true;
        detection_hooks.UnhookAll();

        openglx_hooked = false;
        //vulkan_hooked = false;

        delete openglx_hook; openglx_hook = nullptr;
        //delete vulkan_hook; vulkan_hook = nullptr;
    }

public:
    std::future<ingame_overlay::Renderer_Hook*> detect_renderer(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lk(stop_detection_mutex);

        if (detection_count == 0)
        {// If we have no detections in progress, restart detection.
            detection_cancelled = false;
        }

        ++detection_count;

        return std::async(std::launch::async, [this, timeout]() -> ingame_overlay::Renderer_Hook*
        {
            std::unique_lock<std::timed_mutex> detection_lock(detector_mutex, std::defer_lock);
            constexpr std::chrono::milliseconds infinite_timeout{ -1 };
        
            if (!detection_lock.try_lock_for(timeout))
                return nullptr;

            bool cancel = false;
            {
                System::scoped_lock lk(renderer_mutex, stop_detection_mutex);

                if (!detection_cancelled)
                {
                    if (detection_done)
                    {
                        if (renderer_hook == nullptr)
                        {// Renderer detection was run but we didn't find it, restart the detection
                            detection_done = false;
                        }
                        else
                        {// Renderer already detected, cancel detection and return the renderer.
                            cancel = true;
                        }
                    }

                    if (!EnterDetection())
                        cancel = true;
                }
                else
                {// Detection was cancelled, cancel this detection
                    cancel = true;
                }
            }

            if (cancel)
            {
                --detection_count;
                stop_detection_cv.notify_all();
                return renderer_hook;
            }

            SPDLOG_TRACE("Started renderer detection.");

            std::pair<std::string, void(Renderer_Detector::*)(std::string const&)> libraries[]{
                { OpenGLX_Hook::DLL_NAME, &Renderer_Detector::hook_openglx },
            };
            std::string name;

            auto start_time = std::chrono::steady_clock::now();
            do
            {
                std::unique_lock<std::mutex> lck(stop_detection_mutex);
                if (detection_cancelled || detection_done)
                    break;

                for (auto const& library : libraries)
                {
                    std::string lib_path = FindPreferedModulePath(library.first);
                    if (!lib_path.empty())
                    {
                        void* lib_handle = System::Library::GetLibraryHandle(lib_path.c_str());
                        if (lib_handle != nullptr)
                        {
                            std::lock_guard<std::mutex> lk(renderer_mutex);
                            (this->*library.second)(System::Library::GetLibraryPath(lib_handle));
                        }
                    }
                }

                stop_detection_cv.wait_for(lck, std::chrono::milliseconds{ 100 });
            } while (timeout == infinite_timeout || (std::chrono::steady_clock::now() - start_time) <= timeout);

            {
                System::scoped_lock lk(renderer_mutex, stop_detection_mutex);
                
                ExitDetection();

                --detection_count;
            }
            stop_detection_cv.notify_all();

            SPDLOG_TRACE("Renderer detection done {}.", (void*)renderer_hook);

            return renderer_hook;
        });
    }

    void stop_detection()
    {
        {
            std::lock_guard<std::mutex> lk(stop_detection_mutex);
            if (detection_count == 0)
                return;
        }
        {
            System::scoped_lock lk(renderer_mutex, stop_detection_mutex);
            detection_cancelled = true;
        }
        stop_detection_cv.notify_all();
        {
            std::unique_lock<std::mutex> lk(stop_detection_mutex);
            stop_detection_cv.wait(lk, [&]() { return detection_count == 0; });
        }
    }
};

Renderer_Detector* Renderer_Detector::instance = nullptr;

namespace ingame_overlay {

std::future<ingame_overlay::Renderer_Hook*> DetectRenderer(std::chrono::milliseconds timeout)
{
    return Renderer_Detector::Inst()->detect_renderer(timeout);
}

void StopRendererDetection()
{
    Renderer_Detector::Inst()->stop_detection();
}

void FreeDetector()
{
    delete Renderer_Detector::Inst();
}

}