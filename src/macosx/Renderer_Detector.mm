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

#include "OpenGL_Hook.h"
#include "Metal_Hook.h"

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
        
        delete opengl_hook;
        
        instance = nullptr;
    }
    
private:
    struct driver_hook_t
    {
        // Hook definition
        const char* command_buffer_class;
        const char* render_command_encoder_class;
        IMP hook_command_buffer_commit;

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

    std::timed_mutex detector_mutex;
    std::mutex renderer_mutex;
    
    Base_Hook detection_hooks;
    ingame_overlay::Renderer_Hook* renderer_hook;
    
    bool detection_done;
    uint32_t detection_count;
    bool detection_cancelled;
    std::condition_variable stop_detection_cv;
    std::mutex stop_detection_mutex;
    
    decltype(::CGLFlushDrawable)* CGLFlushDrawable;

    driver_hook_t driver_hooks[DriverCount];
    
    bool opengl_hooked;
    bool metal_hooked;
    
    OpenGL_Hook* opengl_hook;
    Metal_Hook* metal_hook;
    
    Renderer_Detector() :
        CGLFlushDrawable(nullptr),
        opengl_hooked(false),
        metal_hooked(false),
        renderer_hook(nullptr),
        opengl_hook(nullptr),
        metal_hook(nullptr),
        detection_done(false),
        detection_count(0),
        detection_cancelled(false)
    {
        // IGAccel => Intel Graphics Acceleration
        driver_hooks[IntelDriver].command_buffer_class = "MTLIGAccelCommandBuffer";
        driver_hooks[IntelDriver].render_command_encoder_class = "MTLIGAccelRenderCommandEncoder";
        driver_hooks[IntelDriver].hook_command_buffer_commit = (IMP)&MyIGAccelCommandBufferCommit;
        driver_hooks[IntelDriver].CommandBufferCommit = nullptr;
        driver_hooks[IntelDriver].CommandBufferCommitMethod = nil;
        driver_hooks[IntelDriver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        driver_hooks[IntelDriver].RenderCommandEncoderEndEncodingMethod = nil;

        // NVMTL => NVidia WebDriver
        driver_hooks[NVidiaDriver].command_buffer_class = "NVMTLCommandBuffer";
        driver_hooks[NVidiaDriver].render_command_encoder_class = "NVMTLRenderCommandEncoder_PASCAL_B";
        driver_hooks[NVidiaDriver].hook_command_buffer_commit = (IMP)&MyNVMTLCommandBufferCommit;
        driver_hooks[NVidiaDriver].CommandBufferCommit = nullptr;
        driver_hooks[NVidiaDriver].CommandBufferCommitMethod = nil;
        driver_hooks[NVidiaDriver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        driver_hooks[NVidiaDriver].RenderCommandEncoderEndEncodingMethod = nil;

        // AGXG13XFamily => Mac M1 ??
        driver_hooks[M1Driver].command_buffer_class = "AGXG13XFamilyCommandBuffer";
        driver_hooks[M1Driver].render_command_encoder_class = "AGXG13XFamilyRenderContext";
        driver_hooks[M1Driver].hook_command_buffer_commit = (IMP)&MyAGXG13XFamilyCommandBufferCommit;
        driver_hooks[M1Driver].CommandBufferCommit = nullptr;
        driver_hooks[M1Driver].CommandBufferCommitMethod = nil;
        driver_hooks[M1Driver].CommandBufferRenderCommandWithDescriptorMethod = nil;
        driver_hooks[M1Driver].RenderCommandEncoderEndEncodingMethod = nil;
    }
    
    std::string FindPreferedModulePath(std::string const& name)
    {
        return name;
    }
    
    static CGLError MyCGLFlushDrawable(CGLContextObj glDrawable)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        CGLError res = inst->CGLFlushDrawable(glDrawable);
        if (inst->detection_done)
            return res;
        
        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(2, 0))
        {
            inst->renderer_hook = static_cast<ingame_overlay::Renderer_Hook*>(inst->opengl_hook);
            inst->opengl_hook = nullptr;
            inst->StopHooks();
        }
        
        return res;
    }
    
    void _FoundMetalRenderer(int driver, id self, SEL sel)
    {
        driver_hook_t& driver_hook = driver_hooks[driver];
        driver_hook.CommandBufferCommit(self, sel);

        if (detection_done)
            return;

        metal_hook->LoadFunctions(driver_hook.CommandBufferRenderCommandWithDescriptorMethod, driver_hook.RenderCommandEncoderEndEncodingMethod);
        renderer_hook = static_cast<ingame_overlay::Renderer_Hook*>(metal_hook);
        metal_hook = nullptr;
        StopHooks();
    }

    static void MyIGAccelCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->_FoundMetalRenderer(IntelDriver, self, sel);
    }

    static void MyNVMTLCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->_FoundMetalRenderer(NVidiaDriver, self, sel);
    }
    
    static void MyAGXG13XFamilyCommandBufferCommit(id self, SEL sel)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->renderer_mutex);
        inst->_FoundMetalRenderer(M1Driver, self, sel);
    }
    
    void HookglFlushDrawable(decltype(::CGLFlushDrawable)* _CGLFlushDrawable)
    {
        CGLFlushDrawable = _CGLFlushDrawable;
        
        detection_hooks.BeginHook();
        detection_hooks.HookFunc(std::pair<void**, void*>{ (void**)&CGLFlushDrawable, (void*)&MyCGLFlushDrawable });
        detection_hooks.EndHook();
    }
    
    void hook_opengl(std::string const& library_path)
    {
        if (!opengl_hooked)
        {
            System::Library::Library libOpenGL;
            if (!libOpenGL.OpenLibrary(library_path, false))
            {
                SPDLOG_WARN("Failed to load {} to detect OpenGL", library_path);
                return;
            }
            
            auto CGLFlushDrawable = libOpenGL.GetSymbol<decltype(::CGLFlushDrawable)>("CGLFlushDrawable");
            if (CGLFlushDrawable != nullptr)
            {
                SPDLOG_INFO("Hooked CGLFlushDrawable to detect OpenGL");
                
                opengl_hooked = true;
                
                opengl_hook = OpenGL_Hook::Inst();
                opengl_hook->LibraryName = library_path;
                opengl_hook->LoadFunctions(CGLFlushDrawable);
                
                HookglFlushDrawable(CGLFlushDrawable);
            }
            else
            {
                SPDLOG_WARN("Failed to Hook CGLFlushDrawable to detect OpenGL");
            }
        }
    }
    
    void hook_metal(std::string const& library_path)
    {
        if (!metal_hooked)
        {
            System::Library::Library libMetal;
            if (!libMetal.OpenLibrary(library_path, false))
            {
                SPDLOG_WARN("Failed to load {} to detect Metal", library_path);
                return;
            }

            int hooked_count = 0;

            Class mtl_class;

            for (auto& driver_hook : driver_hooks)
            {
                mtl_class = objc_getClass(driver_hook.command_buffer_class);
                driver_hook.CommandBufferCommitMethod = class_getInstanceMethod(mtl_class, @selector(commit));
                if (driver_hook.CommandBufferCommitMethod != nil)
                {
                    driver_hook.CommandBufferRenderCommandWithDescriptorMethod = class_getInstanceMethod(mtl_class, @selector(renderCommandEncoderWithDescriptor:));
                    if (driver_hook.CommandBufferRenderCommandWithDescriptorMethod != nil)
                    {
                        mtl_class = objc_getClass(driver_hook.render_command_encoder_class);
                        driver_hook.RenderCommandEncoderEndEncodingMethod = class_getInstanceMethod(mtl_class, @selector(endEncoding));
                        if (driver_hook.RenderCommandEncoderEndEncodingMethod != nil)
                        {
                            driver_hook.CommandBufferCommit = (decltype(driver_hook_t::CommandBufferCommit))method_setImplementation(driver_hook.CommandBufferCommitMethod, driver_hook.hook_command_buffer_commit);
                            if (driver_hook.CommandBufferCommit != nil)
                                ++hooked_count;
                        }
                    }
                }
            }

            if(hooked_count > 0)
            {
                SPDLOG_INFO("Hooked *CommandBuffer::commit to detect Metal");
                    
                metal_hooked = true;
                    
                metal_hook = Metal_Hook::Inst();
                metal_hook->LibraryName = library_path;
            }
        }
    }
    
    void StopHooks()
    {
        detection_done = true;
        detection_hooks.UnhookAll();
        
        if (metal_hooked)
        {
            metal_hooked = false;
            for (auto& driver_hook : driver_hooks)
            {
                if (driver_hook.CommandBufferCommit != nullptr)
                {
                    method_setImplementation(driver_hook.CommandBufferCommitMethod, (IMP)driver_hook.CommandBufferCommit);
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
        StopHooks();
        
        opengl_hooked = false;
        metal_hooked = false;
        
        delete opengl_hook; opengl_hook = nullptr;
        delete metal_hook; metal_hook = nullptr;
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
            {
                --detection_count;
                return nullptr;
            }
                
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
                { OpenGL_Hook::DLL_NAME, &Renderer_Detector::hook_opengl },
                {  Metal_Hook::DLL_NAME, &Renderer_Detector::hook_metal  },
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
