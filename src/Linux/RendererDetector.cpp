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

#include <InGameOverlay/RendererDetector.h>

#include <System/Encoding.hpp>
#include <System/String.hpp>
#include <System/System.h>
#include <System/Library.h>
#include <System/ScopedLock.hpp>
#include <mini_detour/mini_detour.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "OpenGLXHook.h"

#define TRY_HOOK_FUNCTION(NAME, HOOK) do { if (!_DetectionHooks.HookFunc(std::make_pair<void**, void*>(&(void*&)NAME, HOOK))) { \
    SPDLOG_ERROR("Failed to hook {}", #NAME); } } while(0)

namespace InGameOverlay {

class RendererDetector_t
{
    static RendererDetector_t* _Instance;
public:
    static RendererDetector_t* Inst()
    {
        if (_Instance == nullptr)
        {
            _Instance = new RendererDetector_t;
        }
        return _Instance;
    }

    ~RendererDetector_t()
    {
        StopDetection();

        delete _OpenGLXHook;
        //delete _VulkanHook;

        _Instance = nullptr;
    }

private:
    std::timed_mutex _DetectorMutex;
    std::mutex _RendererMutex;

    BaseHook_t _DetectionHooks;
    InGameOverlay::RendererHook_t* _RendererHook;

    bool _DetectionDone;
    uint32_t _DetectionCount;
    bool _DetectionCancelled;
    std::condition_variable _StopDetectionConditionVariable;
    std::mutex _StopDetectionMutex;

    decltype(::glXSwapBuffers)* _GLXSwapBuffers;

    bool _OpenGLXHooked;
    //bool _VulkanHooked;

    OpenGLXHook_t* _OpenGLXHook;
    //VulkanHook_t* _VulkanHook;

    RendererDetector_t() :
        _OpenGLXHooked(false),
        _RendererHook(nullptr),
        _OpenGLXHook(nullptr),
        //_VulkanHook(nullptr),
        _DetectionDone(false),
        _DetectionCount(0),
        _DetectionCancelled(false)
    {}

    std::string _FindPreferedModulePath(std::string const& name)
    {
        return name;
    }

    static void _MyGLXSwapBuffers(Display* dpy, GLXDrawable drawable)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);
        inst->_GLXSwapBuffers(dpy, drawable);
        if (inst->_DetectionDone)
            return;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
        {
            inst->_DetectionHooks.UnhookAll();
            inst->_RendererHook = static_cast<InGameOverlay::RendererHook_t*>(Inst()->_OpenGLXHook);
            inst->_OpenGLXHook = nullptr;
            inst->_DetectionDone = true;
        }
    }

    void _HookGLXSwapBuffers(decltype(::glXSwapBuffers)* __GLXSwapBuffers)
    {
        _GLXSwapBuffers = __GLXSwapBuffers;

        _DetectionHooks.BeginHook();
        TRY_HOOK_FUNCTION(_GLXSwapBuffers, &RendererDetector_t::_MyGLXSwapBuffers);
        _DetectionHooks.EndHook();
    }

    void _HookOpenGLX(std::string const& library_path)
    {
        if (!_OpenGLXHooked)
        {
            System::Library::Library libGLX;
            if (!libGLX.OpenLibrary(library_path, false))
            {
                SPDLOG_WARN("Failed to load {} to detect OpenGLX", library_path);
                return;
            }

            auto _GLXSwapBuffers = libGLX.GetSymbol<decltype(::glXSwapBuffers)>("glXSwapBuffers");
            if (_GLXSwapBuffers != nullptr)
            {
                SPDLOG_INFO("Hooked glXSwapBuffers to detect OpenGLX");

                _OpenGLXHooked = true;

                _OpenGLXHook = OpenGLXHook_t::Inst();
                _OpenGLXHook->LibraryName = library_path;
                _OpenGLXHook->LoadFunctions(_GLXSwapBuffers);

                _HookGLXSwapBuffers(_GLXSwapBuffers);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook glXSwapBuffers to detect OpenGLX");
            }
        }
    }

    bool _EnterDetection()
    {
        return true;
    }

    void _ExitDetection()
    {
        _DetectionDone = true;
        _DetectionHooks.UnhookAll();

        _OpenGLXHooked = false;
        //_VulkanHooked = false;

        delete _OpenGLXHook; _OpenGLXHook = nullptr;
        //delete _VulkanHook; _VulkanHook = nullptr;
    }

public:
    std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lk(_StopDetectionMutex);

        if (_DetectionCount == 0)
        {// If we have no detections in progress, restart detection.
            _DetectionCancelled = false;
        }

        ++_DetectionCount;

        return std::async(std::launch::async, [this, timeout]() -> InGameOverlay::RendererHook_t*
        {
            std::unique_lock<std::timed_mutex> detection_lock(_DetectorMutex, std::defer_lock);
            constexpr std::chrono::milliseconds infinite_timeout{ -1 };
        
            if (!detection_lock.try_lock_for(timeout))
            {
                --_DetectionCount;
                return nullptr;
            }

            bool cancel = false;
            {
                System::scoped_lock lk(_RendererMutex, _StopDetectionMutex);

                if (!_DetectionCancelled)
                {
                    if (_DetectionDone)
                    {
                        if (_RendererHook == nullptr)
                        {// Renderer detection was run but we didn't find it, restart the detection
                            _DetectionDone = false;
                        }
                        else
                        {// Renderer already detected, cancel detection and return the renderer.
                            cancel = true;
                        }
                    }

                    if (!_EnterDetection())
                        cancel = true;
                }
                else
                {// Detection was cancelled, cancel this detection
                    cancel = true;
                }
            }

            if (cancel)
            {
                --_DetectionCount;
                _StopDetectionConditionVariable.notify_all();
                return _RendererHook;
            }

            SPDLOG_TRACE("Started renderer detection.");

            std::pair<std::string, void(RendererDetector_t::*)(std::string const&)> libraries[]{
                { OpenGLXHook_t::DLL_NAME, &RendererDetector_t::_HookOpenGLX },
            };
            std::string name;

            auto start_time = std::chrono::steady_clock::now();
            do
            {
                std::unique_lock<std::mutex> lck(_StopDetectionMutex);
                if (_DetectionCancelled || _DetectionDone)
                    break;

                for (auto const& library : libraries)
                {
                    std::string lib_path = _FindPreferedModulePath(library.first);
                    if (!lib_path.empty())
                    {
                        void* lib_handle = System::Library::GetLibraryHandle(lib_path.c_str());
                        if (lib_handle != nullptr)
                        {
                            std::lock_guard<std::mutex> lk(_RendererMutex);
                            (this->*library.second)(System::Library::GetLibraryPath(lib_handle));
                        }
                    }
                }

                _StopDetectionConditionVariable.wait_for(lck, std::chrono::milliseconds{ 100 });
            } while (timeout == infinite_timeout || (std::chrono::steady_clock::now() - start_time) <= timeout);

            {
                System::scoped_lock lk(_RendererMutex, _StopDetectionMutex);
                
                _ExitDetection();

                --_DetectionCount;
            }
            _StopDetectionConditionVariable.notify_all();

            SPDLOG_TRACE("Renderer detection done {}.", (void*)_RendererHook);

            return _RendererHook;
        });
    }

    void StopDetection()
    {
        {
            std::lock_guard<std::mutex> lk(_StopDetectionMutex);
            if (_DetectionCount == 0)
                return;
        }
        {
            System::scoped_lock lk(_RendererMutex, _StopDetectionMutex);
            _DetectionCancelled = true;
        }
        _StopDetectionConditionVariable.notify_all();
        {
            std::unique_lock<std::mutex> lk(_StopDetectionMutex);
            _StopDetectionConditionVariable.wait(lk, [&]() { return _DetectionCount == 0; });
        }
    }
};

RendererDetector_t* RendererDetector_t::_Instance = nullptr;

std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout)
{
    return RendererDetector_t::Inst()->DetectRenderer(timeout);
}

void StopRendererDetection()
{
    RendererDetector_t::Inst()->StopDetection();
}

void FreeDetector()
{
    delete RendererDetector_t::Inst();
}

}// namespace InGameOverlay