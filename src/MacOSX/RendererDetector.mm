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
#include <mutex>
#include <string>
#include <string_view>

#include <InGameOverlay/RendererDetector.h>

#include <System/Encoding.hpp>
#include <System/String.hpp>
#include <System/System.h>
#include <System/Library.h>
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

static constexpr const char OPENGL_DLL_NAME[] = "OpenGL";
static constexpr const char METAL_DLL_NAME[] = "Metal";

static std::string FindPreferedModulePath(std::string const& name)
{
    return name;
}

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
        _ExitDetection();
        
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
    };

    std::recursive_mutex _RendererMutex;
    
    BaseHook_t _DetectionHooks;
    RendererHook_t* _RendererHook;
    
    bool _DetectionStarted;
    bool _DetectionDone;
    
    struct DetectionDetails_t
    {
        RendererHookType_t RendererType;
        std::string DllName;
        void (RendererDetector_t::* DetectionProcedure)(std::string_view const&, bool);
    };

    std::array<DetectionDetails_t, 2> RendererLibraries{
        DetectionDetails_t{ RendererHookType_t::OpenGL, OPENGL_DLL_NAME, &RendererDetector_t::_HookOpenGL },
        DetectionDetails_t{ RendererHookType_t::Metal , METAL_DLL_NAME , &RendererDetector_t::_HookMetal  },
    };

    Method _NSOpenGLContextFlushBufferMethod;
    CGLError (*_NSOpenGLContextFlushBuffer)(id self);
    decltype(::CGLFlushDrawable)* _CGLFlushDrawable;

    std::array<MetalDriverHook_t, 3> _MetalDriversHooks = {
        MetalDriverHook_t{ "MTLIGAccelCommandBuffer"   , "MTLIGAccelRenderCommandEncoder"    , (IMP)&_MyIGAccelCommandBufferCommit      , nullptr, nil, nil, nil }, // IGAccel => Intel Graphics Acceleration
        MetalDriverHook_t{ "NVMTLCommandBuffer"        , "NVMTLRenderCommandEncoder_PASCAL_B", (IMP)&_MyNVMTLCommandBufferCommit        , nullptr, nil, nil, nil }, // NVMTL => NVidia WebDriver
        MetalDriverHook_t{ "AGXG13XFamilyCommandBuffer", "AGXG13XFamilyRenderContext"        , (IMP)&_MyAGXG13XFamilyCommandBufferCommit, nullptr, nil, nil, nil }, // AGXG13XFamily => Mac M1 ??
    };
    
    bool _OpenGLHooked;
    bool _MetalHooked;
    
    OpenGLHook_t* _OpenGLHook;
    MetalHook_t* _MetalHook;
    
    RendererDetector_t() :
        _RendererHook(nullptr),
        _DetectionStarted(false),
        _DetectionDone(false),
        _NSOpenGLContextFlushBufferMethod(nullptr),
        _NSOpenGLContextFlushBuffer(nullptr),
        _CGLFlushDrawable(nullptr),
        _OpenGLHooked(false),
        _MetalHooked(false),
        _OpenGLHook(nullptr),
        _MetalHook(nullptr)
    {
    }
    
    void _FoundOpenGLRenderer(bool useObjectiveCMethod)
    {
        if (!_DetectionStarted || _DetectionDone)
            return;

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
        INGAMEOVERLAY_WARN("Called CGLFlushDrawable hook");
        auto inst = Inst();
        std::lock_guard<std::recursive_mutex> lk(inst->_RendererMutex);

        // If the app uses the C function, hook it, else prefer the ObjectiveC method.
        CGLError res = inst->_CGLFlushDrawable(glDrawable);
        inst->_FoundOpenGLRenderer(false);

        return res;
    }

    static CGLError _MyNSOpenGLContextFlushBuffer(id self)
    {
        INGAMEOVERLAY_WARN("Called NSOpenGLContextFlushBuffer hook");
        auto inst = Inst();
        std::lock_guard<std::recursive_mutex> lk(inst->_RendererMutex);

        CGLError res = inst->_NSOpenGLContextFlushBuffer(self);
        inst->_FoundOpenGLRenderer(true);

        return res;
    }
    
    void _FoundMetalRenderer(int driver, id self, SEL sel)
    {
        auto& driverHook = _MetalDriversHooks[driver];
        driverHook.CommandBufferCommit(self, sel);

        if (!_DetectionStarted || _DetectionDone)
            return;

        _MetalHook->LoadFunctions(driverHook.CommandBufferRenderCommandWithDescriptorMethod, driverHook.RenderCommandEncoderEndEncodingMethod);
        _RendererHook = static_cast<InGameOverlay::RendererHook_t*>(_MetalHook);
        _MetalHook = nullptr;
        _StopHooks();
    }

    static void _MyIGAccelCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::recursive_mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(IntelDriver, self, sel);
    }

    static void _MyNVMTLCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::recursive_mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(NVidiaDriver, self, sel);
    }
    
    static void _MyAGXG13XFamilyCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::recursive_mutex> lk(inst->_RendererMutex);
        inst->_FoundMetalRenderer(M1Driver, self, sel);
    }
    
    void _HookCGLFlushDrawable(decltype(::CGLFlushDrawable)* CGLFlushDrawable)
    {
        _CGLFlushDrawable = CGLFlushDrawable;
        
        _DetectionHooks.BeginHook();
        _DetectionHooks.HookFunc(std::pair<void**, void*>{ (void**)&_CGLFlushDrawable, (void*)&_MyCGLFlushDrawable });
        _DetectionHooks.EndHook();
    }
    
    void _HookOpenGL(std::string_view const& libraryPath, bool preferSystemLibraries)
    {
        if (!_OpenGLHooked)
        {
            System::Library::Library libOpenGL;
            if (!libOpenGL.OpenLibrary(libraryPath.data(), false))
            {
                INGAMEOVERLAY_WARN("Failed to load {} to detect OpenGL", libraryPath);
                return;
            }

            auto openGLClass = objc_getClass("NSOpenGLContext");
            _NSOpenGLContextFlushBufferMethod = class_getInstanceMethod(openGLClass, @selector(flushBuffer));

            if (_NSOpenGLContextFlushBufferMethod != nullptr)
            {
                INGAMEOVERLAY_INFO("Hooked NSOpenGLContext::flushBuffer to detect OpenGL");

                _NSOpenGLContextFlushBuffer = (decltype(_NSOpenGLContextFlushBuffer))method_setImplementation(_NSOpenGLContextFlushBufferMethod, (IMP)_MyNSOpenGLContextFlushBuffer);
            }

            auto CGLFlushDrawable = libOpenGL.GetSymbol<decltype(::CGLFlushDrawable)>("CGLFlushDrawable");
            if (CGLFlushDrawable != nullptr)
            {
                INGAMEOVERLAY_INFO("Hooked CGLFlushDrawable to detect OpenGL");

                _OpenGLHooked = true;

                _OpenGLHook = OpenGLHook_t::Inst();
                _OpenGLHook->LibraryName = libraryPath;
                _OpenGLHook->LoadFunctions(nullptr, CGLFlushDrawable);

                _HookCGLFlushDrawable(CGLFlushDrawable);
            }
            else
            {
                INGAMEOVERLAY_WARN("Failed to Hook CGLFlushDrawable to detect OpenGL");
            }
        }
    }
    
    void _HookMetal(std::string_view const& libraryPath, bool preferSystemLibraries)
    {
        if (!_MetalHooked)
        {
            System::Library::Library libMetal;
            if (!libMetal.OpenLibrary(libraryPath.data(), false))
            {
                INGAMEOVERLAY_WARN("Failed to load {} to detect Metal", libraryPath);
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
                INGAMEOVERLAY_INFO("Hooked *CommandBuffer::commit to detect Metal");
                    
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
    
    bool _EnterDetection()
    {
        return true;
    }
    
    void _ExitDetection()
    {
        _StopHooks();
        
        _OpenGLHooked = false;
        _MetalHooked = false;
        
        delete _OpenGLHook; _OpenGLHook = nullptr;
        delete _MetalHook; _MetalHook = nullptr;
    }
    
public:
    bool DetectRenderer(bool restart, RendererHookType_t rendererToDetect, bool preferSystemLibraries)
    {
        auto wantsContinue = false;

        {
            std::lock_guard<std::recursive_mutex> lk(_RendererMutex);

            if (_DetectionDone)
            {
                if (_RendererHook != nullptr || !restart)
                {
                    _ExitDetection();
                    return wantsContinue;
                }

                _DetectionStarted = false;
                _DetectionDone = false;
            }

            wantsContinue = true;

            if (!_EnterDetection())
                return wantsContinue;
        }

        INGAMEOVERLAY_TRACE("Started renderer detection.");

        std::string name;

        for (auto const& library : RendererLibraries)
        {
            if ((rendererToDetect & library.RendererType) != library.RendererType)
                continue;

            std::string libraryPath = preferSystemLibraries ? FindPreferedModulePath(library.DllName) : library.DllName;
            if (!libraryPath.empty())
            {
                void* libraryHandle = System::Library::GetLibraryHandle(libraryPath.c_str());
                if (libraryHandle != nullptr)
                {
                    INGAMEOVERLAY_DEBUG("Waiting for renderer mutex for {}...", libraryPath);
                    std::lock_guard<std::recursive_mutex> lk(_RendererMutex);
                    INGAMEOVERLAY_DEBUG("Got renderer mutex for {}...", libraryPath);
                    if (_DetectionDone)
                        break;

                    (this->*library.DetectionProcedure)(System::Library::GetLibraryPath(libraryHandle), preferSystemLibraries);
                }
            }
        }
        INGAMEOVERLAY_TRACE("Exited renderer detection.");

        {
            std::lock_guard<std::recursive_mutex> lk(_RendererMutex);
            _DetectionStarted = true;

            if (_DetectionDone)
            {
                wantsContinue = false;
                _ExitDetection();
            }
        }

        return wantsContinue;
    }
    
    void StopDetection()
    {
        std::lock_guard<std::recursive_mutex> lk(_RendererMutex);
        _DetectionDone = true;
    }

    RendererHook_t* GetDetectedRenderer()
    {
        std::lock_guard<std::recursive_mutex> lk(_RendererMutex);
        if (!_DetectionDone)
            return nullptr;

        return _RendererHook;
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

        auto logger = std::make_shared<spdlog::logger>(INGAMEOVERLAY_SPDLOG_LOGGER_NAME, sinks);

        logger->set_pattern(INGAMEOVERLAY_SPDLOG_LOG_FORMAT);
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);

        SetLogger(logger);
    });
}

#endif

bool DetectRenderer(bool restart, RendererHookType_t rendererToDetect, bool preferSystemLibraries)
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    return RendererDetector_t::Inst()->DetectRenderer(restart, rendererToDetect, preferSystemLibraries);
}
    
void StopRendererDetection()
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    RendererDetector_t::Inst()->StopDetection();
}

RendererHook_t* GetDetectedRenderer()
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    return RendererDetector_t::Inst()->GetDetectedRenderer();
}

RendererHook_t* GetRenderer(RendererHookType_t rendererToDetect, bool preferSystemLibraries)
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    RendererHook_t* rendererHook = nullptr;

    switch (rendererToDetect)
    {
        case RendererHookType_t::OpenGL: break;
        case RendererHookType_t::Metal: break;
    }

    return rendererHook;
}

void FreeDetector()
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    delete RendererDetector_t::Inst();
}

}// namespace InGameOverlay
