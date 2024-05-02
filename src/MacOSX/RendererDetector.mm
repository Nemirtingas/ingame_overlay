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

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

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

#include "OpenGLHook.h"
#include "MetalHook.h"

#ifdef INGAMEOVERLAY_USE_SPDLOG

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#endif

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
        
        delete _OpenGLHook;
        
        _Instance = nullptr;
    }
    
private:
    struct MetalDriverHook_t
    {
        // Hook definition
        const char* CommandBufferClass;
        const char* RenderCommandEncoderClass;
        IMP HookCommandBufferCommit;

        // ObjC runtime
        void (*CommandBufferCommit)(id, SEL);
        Method CommandBufferCommitMethod;
        Method CommandBufferRenderCommandWithDescriptorMethod;
        Method RenderCommandEncoderEndEncodingMethod;
    };

    enum
    {
        IntelDriver = 0,
        NVidiaDriver = 1,
        M1Driver = 2,
        DriverCount = 3,
    };

    std::timed_mutex _DetectorMutex;
    std::mutex _RendererMutex;
    
    BaseHook_t _DetectionHooks;
    RendererHook_t* _RendererHook;
    
    bool _DetectionDone;
    uint32_t _DetectionCount;
    bool _DetectionCancelled;
    std::condition_variable _StopDetectionConditionVariable;
    std::mutex _StopDetectionMutex;
    
    Method _NSOpenGLContextFlushBufferMethod;
    CGLError (*_NSOpenGLContextFlushBuffer)(id self);
    decltype(::CGLFlushDrawable)* _CGLFlushDrawable;

    MetalDriverHook_t _MetalDriversHooks[DriverCount];
    
    bool _OpenGLHooked;
    bool _MetalHooked;
    
    OpenGLHook_t* _OpenGLHook;
    MetalHook_t* _MetalHook;
    
    RendererDetector_t() :
        _CGLFlushDrawable(nullptr),
        _OpenGLHooked(false),
        _MetalHooked(false),
        _RendererHook(nullptr),
        _OpenGLHook(nullptr),
        _MetalHook(nullptr),
        _DetectionDone(false),
        _DetectionCount(0),
        _DetectionCancelled(false)
    {
        // IGAccel => Intel Graphics Acceleration
        _MetalDriversHooks[IntelDriver].CommandBufferClass = "MTLIGAccelCommandBuffer";
        _MetalDriversHooks[IntelDriver].RenderCommandEncoderClass = "MTLIGAccelRenderCommandEncoder";
        _MetalDriversHooks[IntelDriver].HookCommandBufferCommit = (IMP)&_MyIGAccelCommandBufferCommit;
        _MetalDriversHooks[IntelDriver].CommandBufferCommit = nullptr;
        _MetalDriversHooks[IntelDriver].CommandBufferCommitMethod = nil;
        _MetalDriversHooks[IntelDriver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        _MetalDriversHooks[IntelDriver].RenderCommandEncoderEndEncodingMethod = nil;

        // NVMTL => NVidia WebDriver
        _MetalDriversHooks[NVidiaDriver].CommandBufferClass = "NVMTLCommandBuffer";
        _MetalDriversHooks[NVidiaDriver].RenderCommandEncoderClass = "NVMTLRenderCommandEncoder_PASCAL_B";
        _MetalDriversHooks[NVidiaDriver].HookCommandBufferCommit = (IMP)&_MyNVMTLCommandBufferCommit;
        _MetalDriversHooks[NVidiaDriver].CommandBufferCommit = nullptr;
        _MetalDriversHooks[NVidiaDriver].CommandBufferCommitMethod = nil;
        _MetalDriversHooks[NVidiaDriver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        _MetalDriversHooks[NVidiaDriver].RenderCommandEncoderEndEncodingMethod = nil;

        // AGXG13XFamily => Mac M1 ??
        _MetalDriversHooks[M1Driver].CommandBufferClass = "AGXG13XFamilyCommandBuffer";
        _MetalDriversHooks[M1Driver].RenderCommandEncoderClass = "AGXG13XFamilyRenderContext";
        _MetalDriversHooks[M1Driver].HookCommandBufferCommit = (IMP)&_MyAGXG13XFamilyCommandBufferCommit;
        _MetalDriversHooks[M1Driver].CommandBufferCommit = nullptr;
        _MetalDriversHooks[M1Driver].CommandBufferCommitMethod = nil;
        _MetalDriversHooks[M1Driver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        _MetalDriversHooks[M1Driver].RenderCommandEncoderEndEncodingMethod = nil;
    }
    
    std::string _FindPreferedModulePath(std::string const& name)
    {
        return name;
    }
    
    void _FoundOpenGLRenderer(bool useObjectiveCMethod)
    {
        if (useObjectiveCMethod)
            _OpenGLHook->LoadFunctions(_NSOpenGLContextFlushBufferMethod, nullptr);


        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(2, 0))
        {
            _RendererHook = static_cast<InGameOverlay::RendererHook_t*>(_OpenGLHook);
            _OpenGLHook = nullptr;
            _StopHooks();
        }
    }

    static CGLError _MyCGLFlushDrawable(CGLContextObj glDrawable)
    {
        auto inst = Inst();
        std::unique_lock<std::mutex> lk(inst->_RendererMutex, std::defer_lock);

        // If the app uses the C function, hook it, else prefer the ObjectiveC method.
        lk.try_lock();

        CGLError res = inst->_CGLFlushDrawable(glDrawable);
        if (inst->_DetectionDone)
            return res;

        inst->_FoundOpenGLRenderer(false);

        return res;
    }

    static CGLError _MyNSOpenGLContextFlushBuffer(id self)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        CGLError res = inst->_NSOpenGLContextFlushBuffer(self);
        if (inst->_DetectionDone)
            return res;

        inst->_FoundOpenGLRenderer(true);

        return res;
    }

    
    void _FoundMetalRenderer(int driver, id self, SEL sel)
    {
        MetalDriverHook_t& driverHook = _MetalDriversHooks[driver];
        driverHook.CommandBufferCommit(self, sel);

        if (_DetectionDone)
            return;

        _MetalHook->LoadFunctions(driverHook.CommandBufferRenderCommandWithDescriptorMethod, driverHook.RenderCommandEncoderEndEncodingMethod);
        _RendererHook = static_cast<InGameOverlay::RendererHook_t*>(_MetalHook);
        _MetalHook = nullptr;
        _StopHooks();
    }

    static void _MyIGAccelCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(IntelDriver, self, sel);
    }

    static void _MyNVMTLCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(NVidiaDriver, self, sel);
    }
    
    static void _MyAGXG13XFamilyCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(M1Driver, self, sel);
    }
    
    void _HookCGLFlushDrawable(decltype(::CGLFlushDrawable)* CGLFlushDrawable)
    {
        _CGLFlushDrawable = CGLFlushDrawable;
        
        _DetectionHooks.BeginHook();
        _DetectionHooks.HookFunc(std::pair<void**, void*>{ (void**)&_CGLFlushDrawable, (void*)&_MyCGLFlushDrawable });
        _DetectionHooks.EndHook();
    }
    
    void _HookOpenGL(std::string const& libraryPath)
    {
        if (!_OpenGLHooked)
        {
            System::Library::Library libOpenGL;
            if (!libOpenGL.OpenLibrary(libraryPath, false))
            {
                SPDLOG_WARN("Failed to load {} to detect OpenGL", libraryPath);
                return;
            }

            auto openGLClass = objc_getClass("NSOpenGLContext");
            _NSOpenGLContextFlushBufferMethod = class_getInstanceMethod(openGLClass, @selector(flushBuffer));

            if (_NSOpenGLContextFlushBufferMethod != nullptr)
            {
                SPDLOG_INFO("Hooked NSOpenGLContext::flushBuffer to detect OpenGL");

                _NSOpenGLContextFlushBuffer = (decltype(_NSOpenGLContextFlushBuffer))method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)_MyNSOpenGLContextFlushBuffer);
            }

            auto CGLFlushDrawable = libOpenGL.GetSymbol<decltype(::CGLFlushDrawable)>("CGLFlushDrawable");
            if (CGLFlushDrawable != nullptr)
            {
                SPDLOG_INFO("Hooked CGLFlushDrawable to detect OpenGL");

                _OpenGLHooked = true;

                _OpenGLHook = OpenGLHook_t::Inst();
                _OpenGLHook->LibraryName = libraryPath;
                _OpenGLHook->LoadFunctions(nullptr, CGLFlushDrawable);

                _HookCGLFlushDrawable(CGLFlushDrawable);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook CGLFlushDrawable to detect OpenGL");
            }
        }
    }
    
    void _HookMetal(std::string const& libraryPath)
    {
        if (!_MetalHooked)
        {
            System::Library::Library libMetal;
            if (!libMetal.OpenLibrary(libraryPath, false))
            {
                SPDLOG_WARN("Failed to load {} to detect Metal", libraryPath);
                return;
            }

            int hooked_count = 0;

            Class metalClass;

            for (auto& driverHook : _MetalDriversHooks)
            {
                metalClass = objc_getClass(driverHook.CommandBufferClass);
                driverHook.CommandBufferCommitMethod = class_getInstanceMethod(metalClass, @selector(commit));
                if (driverHook.CommandBufferCommitMethod != nil)
                {
                    driverHook.CommandBufferRenderCommandWithDescriptorMethod = class_getInstanceMethod(metalClass, @selector(renderCommandEncoderWithDescriptor:));
                    if (driverHook.CommandBufferRenderCommandWithDescriptorMethod != nil)
                    {
                        metalClass = objc_getClass(driverHook.RenderCommandEncoderClass);
                        driverHook.RenderCommandEncoderEndEncodingMethod = class_getInstanceMethod(metalClass, @selector(endEncoding));
                        if (driverHook.RenderCommandEncoderEndEncodingMethod != nil)
                        {
                            driverHook.CommandBufferCommit = (decltype(MetalDriverHook_t::CommandBufferCommit))method_setImplementation(driverHook.CommandBufferCommitMethod, driverHook.HookCommandBufferCommit);
                            if (driverHook.CommandBufferCommit != nil)
                                ++hooked_count;
                        }
                    }
                }
            }

            if(hooked_count > 0)
            {
                SPDLOG_INFO("Hooked *CommandBuffer::commit to detect Metal");
                    
                _MetalHooked = true;
                    
                _MetalHook = MetalHook_t::Inst();
                _MetalHook->LibraryName = libraryPath;
            }
        }
    }
    
    void _StopHooks()
    {
        _DetectionDone = true;
        _DetectionHooks.UnhookAll();
        
        if (_OpenGLHooked)
        {
            _OpenGLHooked = false;
            if (_NSOpenGLContextFlushBuffer != nullptr)
            {
                method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)_NSOpenGLContextFlushBuffer);
                _NSOpenGLContextFlushBufferMethod = nullptr;
                _NSOpenGLContextFlushBuffer = nullptr;
            }
        }

        if (_MetalHooked)
        {
            _MetalHooked = false;
            for (auto& driverHook : _MetalDriversHooks)
            {
                if (driverHook.CommandBufferCommit != nullptr)
                {
                    method_setImplementation(driverHook.CommandBufferCommitMethod, (IMP)driverHook.CommandBufferCommit);
                }
            }
        }
    }
    
    bool EnterDetection()
    {
        return true;
    }
    
    void ExitDetection()
    {
        _StopHooks();
        
        _OpenGLHooked = false;
        _MetalHooked = false;
        
        delete _OpenGLHook; _OpenGLHook = nullptr;
        delete _MetalHook; _MetalHook = nullptr;
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
            constexpr std::chrono::milliseconds infiniteTimeout{ -1 };

            if (!detection_lock.try_lock_for(timeout))
            {
                --_DetectionCount;
                return nullptr;
            }
                
            bool cancel = false;
            {
                auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);

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
                --_DetectionCount;
                _StopDetectionConditionVariable.notify_all();
                return _RendererHook;
            }

            SPDLOG_TRACE("Started renderer detection.");

            std::pair<std::string, void(RendererDetector_t::*)(std::string const&)> libraries[]{
                { OpenGLHook_t::DLL_NAME, &RendererDetector_t::_HookOpenGL },
                {  MetalHook_t::DLL_NAME, &RendererDetector_t::_HookMetal  },
            };
            std::string name;

            auto startTime = std::chrono::steady_clock::now();
            do
            {
                std::unique_lock<std::mutex> lck(_StopDetectionMutex);
                if (_DetectionCancelled || _DetectionDone)
                    break;

                for (auto const& library : libraries)
                {
                    std::string libraryPath = _FindPreferedModulePath(library.first);
                    if (!libraryPath.empty())
                    {
                        void* libraryHandle = System::Library::GetLibraryHandle(libraryPath.c_str());
                        if (libraryHandle != nullptr)
                        {
                            std::lock_guard<std::mutex> lk(_RendererMutex);
                            (this->*library.second)(System::Library::GetLibraryPath(libraryHandle));
                        }
                    }
                }

                _StopDetectionConditionVariable.wait_for(lck, std::chrono::milliseconds{ 100 });
            } while (timeout == infiniteTimeout || (std::chrono::steady_clock::now() - startTime) <= timeout);

            {
                auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);

                ExitDetection();

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
            auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);
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
    
#ifdef INGAMEOVERLAY_USE_SPDLOG

static inline void SetupSpdLog()
{   
    static std::once_flag once;
    std::call_once(once, []()
    {
        auto sinks = std::make_shared<spdlog::sinks::dist_sink_mt>();

        sinks->add_sink(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

        auto logger = std::make_shared<spdlog::logger>("RendererDetectorDebugLogger", sinks);

        spdlog::register_logger(logger);

        logger->set_pattern("[%H:%M:%S.%e](%t)[%l] - %!{%#} - %v");
        spdlog::set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    });
}

#endif

std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout)
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
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