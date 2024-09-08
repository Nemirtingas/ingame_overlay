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
#include "../VulkanHelpers.h"

#include <System/Encoding.hpp>
#include <System/String.hpp>
#include <System/System.h>
#include <System/Library.h>
#include <System/ScopedLock.hpp>
#include <mini_detour/mini_detour.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "OpenGLXHook.h"
#include "VulkanHook.h"

#define TRY_HOOK_FUNCTION(NAME, HOOK) do { if (!_DetectionHooks.HookFunc(std::make_pair<void**, void*>(&(void*&)NAME, (void*)HOOK))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME); } } while(0)

#ifdef INGAMEOVERLAY_USE_SPDLOG

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#endif

namespace InGameOverlay {

static constexpr const char OPENGLX_DLL_NAME[] = "libGLX.so";
static constexpr const char VULKAN_DLL_NAME[] = "libvulkan.so";

struct OpenGLDriver_t
{
    std::string LibraryPath;
    decltype(::glXSwapBuffers)* glXSwapBuffers;
};

struct VulkanDriver_t
{
    std::string LibraryPath;

    std::function<void* (const char*)> vkLoader;
    decltype(::vkAcquireNextImageKHR)* vkAcquireNextImageKHR;
    decltype(::vkAcquireNextImage2KHR)* vkAcquireNextImage2KHR;
    decltype(::vkQueuePresentKHR)* vkQueuePresentKHR;
    decltype(::vkCreateSwapchainKHR)* vkCreateSwapchainKHR;
    decltype(::vkDestroyDevice)* vkDestroyDevice;
};

static std::string FindPreferedModulePath(std::string const& name)
{
    return name;
}

static OpenGLDriver_t GetOpenGLDriver(std::string const& openGLLibraryPath)
{
    OpenGLDriver_t driver{};

    void* hOpenGL = System::Library::GetLibraryHandle(openGLLibraryPath.c_str());
    if (hOpenGL == nullptr)
    {
        INGAMEOVERLAY_WARN("Failed to load {} to detect OpenGLX", openGLLibraryPath);
        return driver;
    }

    driver.glXSwapBuffers = (decltype(::glXSwapBuffers)*)System::Library::GetSymbol(hOpenGL, "glXSwapBuffers");
    driver.LibraryPath = System::Library::GetLibraryPath(hOpenGL);
    return driver;
}

static VulkanDriver_t GetVulkanDriver(std::string const& vulkanLibraryPath)
{
    VulkanDriver_t driver{};

    void* hVulkan = System::Library::GetLibraryHandle(vulkanLibraryPath.c_str());
    if (hVulkan == nullptr)
    {
        INGAMEOVERLAY_WARN("Failed to load {} to detect Vulkan", vulkanLibraryPath);
        return driver;
    }

    std::function<void* (const char*)> _vkLoader = [hVulkan](const char* symbolName)
    {
        return System::Library::GetSymbol(hVulkan, symbolName);
    };

    auto _vkCreateInstance = (decltype(::vkCreateInstance)*)_vkLoader("vkCreateInstance");
    auto _vkDestroyInstance = (decltype(::vkDestroyInstance)*)_vkLoader("vkDestroyInstance");
    auto _vkGetInstanceProcAddr = (decltype(::vkGetInstanceProcAddr)*)_vkLoader("vkGetInstanceProcAddr");
    auto _vkEnumeratePhysicalDevices = (decltype(::vkEnumeratePhysicalDevices)*)_vkLoader("vkEnumeratePhysicalDevices");
    auto _vkGetPhysicalDeviceProperties = (decltype(::vkGetPhysicalDeviceProperties)*)_vkLoader("vkGetPhysicalDeviceProperties");
    auto _vkGetPhysicalDeviceQueueFamilyProperties = (decltype(::vkGetPhysicalDeviceQueueFamilyProperties)*)_vkLoader("vkGetPhysicalDeviceQueueFamilyProperties");
    auto _vkEnumerateDeviceExtensionProperties = (decltype(::vkEnumerateDeviceExtensionProperties)*)_vkLoader("vkEnumerateDeviceExtensionProperties");
    auto _vkCreateDevice = (decltype(::vkCreateDevice)*)_vkLoader("vkCreateDevice");
    auto _vkDestroyDevice = (decltype(::vkDestroyDevice)*)_vkLoader("vkDestroyDevice");
    auto _vkGetDeviceProcAddr = (decltype(::vkGetDeviceProcAddr)*)_vkLoader("vkGetDeviceProcAddr");

    VkInstance _vkInstance = nullptr;
    VkPhysicalDevice _vkPhysicalDevice = nullptr;
    VkDevice _vkDevice = nullptr;

    if (_vkCreateInstance == nullptr ||
        _vkDestroyInstance == nullptr ||
        _vkGetInstanceProcAddr == nullptr ||
        _vkEnumeratePhysicalDevices == nullptr ||
        _vkGetPhysicalDeviceProperties == nullptr ||
        _vkGetPhysicalDeviceQueueFamilyProperties == nullptr ||
        _vkCreateDevice == nullptr ||
        _vkDestroyDevice == nullptr ||
        _vkGetDeviceProcAddr == nullptr)
    {
        return driver;
    }

    {
        VkInstanceCreateInfo info = { };
        const char* instanceExtension = VK_KHR_SURFACE_EXTENSION_NAME;

        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &instanceExtension;

        _vkCreateInstance(&info, nullptr, &_vkInstance);
    }
    {
        uint32_t physicalDeviceCount;
        std::vector<VkPhysicalDevice> physicalDevices;
        VkPhysicalDeviceProperties physicalDeviceProperties;

        _vkEnumeratePhysicalDevices(_vkInstance, &physicalDeviceCount, NULL);
        physicalDevices.resize(physicalDeviceCount);
        _vkEnumeratePhysicalDevices(_vkInstance, &physicalDeviceCount, physicalDevices.data());

        int selectedDevicetype = SelectDeviceTypeStart;

        for (uint32_t i = 0; i < physicalDeviceCount; ++i)
        {
            _vkGetPhysicalDeviceProperties(physicalDevices[i], &physicalDeviceProperties);
            if (!VulkanPhysicalDeviceHasExtension(_vkEnumerateDeviceExtensionProperties, physicalDevices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;

            if (SelectVulkanPhysicalDeviceType(physicalDeviceProperties.deviceType, selectedDevicetype))
            {
                _vkPhysicalDevice = physicalDevices[i];
                if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    break;
            }
        }
    }
    int32_t queueFamilyIndex = -1;
    if (_vkPhysicalDevice == nullptr || (queueFamilyIndex = GetVulkanPhysicalDeviceFirstGraphicsQueue(_vkGetPhysicalDeviceQueueFamilyProperties, _vkPhysicalDevice)) == -1)
    {
        _vkDestroyInstance(_vkInstance, nullptr);
        return driver;
    }

    {
        const char* deviceExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        const float queuePriority = 1.0f;

        VkDeviceQueueCreateInfo queueInfo = { };
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo createInfo = { };
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &deviceExtension;

        if (_vkCreateDevice(_vkPhysicalDevice, &createInfo, nullptr, &_vkDevice) != VkResult::VK_SUCCESS)
        {
            _vkDestroyInstance(_vkInstance, nullptr);
            return driver;
        }
    }

    auto _vkAcquireNextImageKHR = (decltype(::vkAcquireNextImageKHR)*)_vkGetDeviceProcAddr(_vkDevice, "vkAcquireNextImageKHR");
    auto _vkAcquireNextImage2KHR = (decltype(::vkAcquireNextImage2KHR)*)_vkGetDeviceProcAddr(_vkDevice, "vkAcquireNextImage2KHR");
    auto _vkQueuePresentKHR = (decltype(::vkQueuePresentKHR)*)_vkGetDeviceProcAddr(_vkDevice, "vkQueuePresentKHR");
    auto _vkCreateSwapchainKHR = (decltype(::vkCreateSwapchainKHR)*)_vkGetDeviceProcAddr(_vkDevice, "vkCreateSwapchainKHR");

    _vkDestroyDevice(_vkDevice, nullptr);
    _vkDestroyInstance(_vkInstance, nullptr);

    if (_vkAcquireNextImageKHR == nullptr ||
        _vkQueuePresentKHR == nullptr ||
        _vkCreateSwapchainKHR == nullptr)
    {
        return driver;
    }

    driver.vkLoader = std::move(_vkLoader);

    driver.vkAcquireNextImageKHR = _vkAcquireNextImageKHR;
    driver.vkAcquireNextImage2KHR = _vkAcquireNextImage2KHR;
    driver.vkQueuePresentKHR = _vkQueuePresentKHR;
    driver.vkCreateSwapchainKHR = _vkCreateSwapchainKHR;
    driver.vkDestroyDevice = _vkDestroyDevice;

    driver.LibraryPath = System::Library::GetLibraryPath(hVulkan);
    return driver;
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
        StopDetection();

        delete _OpenGLXHook;
        delete _VulkanHook;

        _Instance = nullptr;
    }

private:
    std::timed_mutex _DetectorMutex;
    std::mutex _RendererMutex;

    BaseHook_t _DetectionHooks;
    RendererHook_t* _RendererHook;

    bool _DetectionStarted;
    bool _DetectionDone;
    uint32_t _DetectionCount;
    bool _DetectionCancelled;
    std::condition_variable _StopDetectionConditionVariable;
    std::mutex _StopDetectionMutex;

    decltype(::glXSwapBuffers)* _GLXSwapBuffers;
    decltype(::vkQueuePresentKHR)* _VkQueuePresentKHR;

    bool _OpenGLXHooked;
    bool _VulkanHooked;

    OpenGLXHook_t* _OpenGLXHook;
    VulkanHook_t* _VulkanHook;

    RendererDetector_t() :
        _RendererHook(nullptr),
        _DetectionStarted(false),
        _DetectionDone(false),
        _DetectionCount(0),
        _DetectionCancelled(false),
        _GLXSwapBuffers(nullptr),
        _VkQueuePresentKHR(nullptr),
        _OpenGLXHooked(false),
        _VulkanHooked(false),
        _OpenGLXHook(nullptr),
        _VulkanHook(nullptr)
    {}

    template<typename T>
    void _HookDetected(T*& detected_renderer)
    {
        _DetectionHooks.UnhookAll();
        _RendererHook = static_cast<InGameOverlay::RendererHook_t*>(detected_renderer);
        detected_renderer = nullptr;
        _DetectionDone = true;
    }

    static void _MyGLXSwapBuffers(Display* dpy, GLXDrawable drawable)
    {
        auto inst = Inst();
        std::lock_guard<std::mutex> lk(inst->_RendererMutex);

        INGAMEOVERLAY_INFO("glXSwapBuffers");
        inst->_GLXSwapBuffers(dpy, drawable);
        if (!inst->_DetectionStarted || inst->_DetectionDone)
            return;

        if (gladLoaderLoadGL() >= GLAD_MAKE_VERSION(3, 1))
            inst->_HookDetected(inst->_OpenGLXHook);
    }

    static VkResult VKAPI_CALL _MyvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        auto inst = Inst();
        std::unique_lock<std::mutex> lk(inst->_RendererMutex, std::try_to_lock);

        INGAMEOVERLAY_INFO("vkQueuePresentKHR");
        auto res = inst->_VkQueuePresentKHR(queue, pPresentInfo);
        if (!inst->_DetectionStarted || !inst->_VulkanHooked || inst->_DetectionDone)
            return res;

        inst->_HookDetected(inst->_VulkanHook);

        return res;
    }

    void _HookOpenGLX(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_OpenGLXHooked)
        {
            auto driver = GetOpenGLDriver(libraryPath);
            if (driver.glXSwapBuffers != nullptr)
            {
                INGAMEOVERLAY_INFO("Hooked glXSwapBuffers to detect OpenGLX");
                _OpenGLXHooked = true;

                _GLXSwapBuffers = driver.glXSwapBuffers;

                _OpenGLXHook = OpenGLXHook_t::Inst();
                _OpenGLXHook->LibraryName = driver.LibraryPath;
                _OpenGLXHook->LoadFunctions(_GLXSwapBuffers);

                _DetectionHooks.BeginHook();
                TRY_HOOK_FUNCTION(_GLXSwapBuffers, &RendererDetector_t::_MyGLXSwapBuffers);
                _DetectionHooks.EndHook();
            }
            else
            {
                INGAMEOVERLAY_WARN("Failed to Hook glXSwapBuffers to detect OpenGLX");
            }
        }
    }

    void _HookVulkan(std::string const& libraryPath, bool preferSystemLibraries)
    {
        if (!_VulkanHooked)
        {
            auto driver = GetVulkanDriver(libraryPath);
            if (driver.vkQueuePresentKHR != nullptr)
            {
                INGAMEOVERLAY_INFO("Hooked vkQueuePresentKHR to detect Vulkan");
                _VulkanHooked = true;

                _VkQueuePresentKHR = driver.vkQueuePresentKHR;

                _VulkanHook = VulkanHook_t::Inst();
                _VulkanHook->LibraryName = driver.LibraryPath;
                _VulkanHook->LoadFunctions(
                    driver.vkLoader,
                    driver.vkAcquireNextImageKHR,
                    driver.vkAcquireNextImage2KHR,
                    driver.vkQueuePresentKHR,
                    driver.vkCreateSwapchainKHR,
                    driver.vkDestroyDevice);
                _VulkanHooked = true;

                _DetectionHooks.BeginHook();
                TRY_HOOK_FUNCTION(_VkQueuePresentKHR, &RendererDetector_t::_MyvkQueuePresentKHR);
                _DetectionHooks.EndHook();
            }
            else
            {
                INGAMEOVERLAY_WARN("Failed to Hook vkQueuePresentKHR to detect Vulkan");
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
        _VulkanHooked = false;

        delete _OpenGLXHook; _OpenGLXHook = nullptr;
        delete _VulkanHook; _VulkanHook = nullptr;
    }

public:
    std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout, RendererHookType_t rendererToDetect, bool preferSystemLibraries)
    {
        std::lock_guard<std::mutex> lk(_StopDetectionMutex);

        if (_DetectionCount == 0)
        {// If we have no detections in progress, restart detection.
            _DetectionCancelled = false;
        }

        ++_DetectionCount;

        return std::async(std::launch::async, [this, timeout, rendererToDetect, preferSystemLibraries]() -> InGameOverlay::RendererHook_t*
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

            INGAMEOVERLAY_TRACE("Started renderer detection.");

            struct DetectionDetails_t
            {
                std::string DllName;
                void (RendererDetector_t::* DetectionProcedure)(std::string const&, bool);
            };

            std::vector<DetectionDetails_t> libraries;
            if ((rendererToDetect & RendererHookType_t::OpenGL) == RendererHookType_t::OpenGL)
                libraries.emplace_back(DetectionDetails_t{ OPENGLX_DLL_NAME, &RendererDetector_t::_HookOpenGLX });

            if ((rendererToDetect & RendererHookType_t::Vulkan) == RendererHookType_t::Vulkan)
                libraries.emplace_back(DetectionDetails_t{ VULKAN_DLL_NAME, &RendererDetector_t::_HookVulkan });

            std::string name;

            auto startTime = std::chrono::steady_clock::now();
            do
            {
                std::unique_lock<std::mutex> lck(_StopDetectionMutex);
                if (_DetectionCancelled || _DetectionDone)
                    break;

                for (auto const& library : libraries)
                {
                    std::string libraryPath = preferSystemLibraries ? FindPreferedModulePath(library.DllName) : library.DllName;
                    if (!libraryPath.empty())
                    {
                        void* libraryHandle = System::Library::GetLibraryHandle(libraryPath.c_str());
                        if (libraryHandle != nullptr)
                        {
                            std::lock_guard<std::mutex> lk(_RendererMutex);
                            (this->*library.DetectionProcedure)(System::Library::GetLibraryPath(libraryHandle), preferSystemLibraries);
                        }
                    }
                }

                _StopDetectionConditionVariable.wait_for(lck, std::chrono::milliseconds{ 100 });
                if (!_DetectionStarted)
                {
                    std::lock_guard<std::mutex> lck(_RendererMutex);
                    _DetectionStarted = true;
                }
            } while (timeout == infiniteTimeout || (std::chrono::steady_clock::now() - startTime) <= timeout);

            _DetectionStarted = false;
            {
                auto lk = System::ScopeLock(_RendererMutex, _StopDetectionMutex);
                
                _ExitDetection();

                --_DetectionCount;
            }
            _StopDetectionConditionVariable.notify_all();

            INGAMEOVERLAY_TRACE("Renderer detection done {}.", (void*)_RendererHook);

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

        auto logger = std::make_shared<spdlog::logger>(INGAMEOVERLAY_SPDLOG_LOGGER_NAME, sinks);

        logger->set_pattern(INGAMEOVERLAY_SPDLOG_LOG_FORMAT);
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);

        SetLogger(logger);
    });
}

#endif

std::future<InGameOverlay::RendererHook_t*> DetectRenderer(std::chrono::milliseconds timeout, RendererHookType_t rendererToDetect, bool preferSystemLibraries)
{
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif
    return RendererDetector_t::Inst()->DetectRenderer(timeout, rendererToDetect, preferSystemLibraries);
}

void StopRendererDetection()
{
    RendererDetector_t::Inst()->StopDetection();
}

void FreeDetector()
{
    delete RendererDetector_t::Inst();
}

RendererHook_t* CreateRendererHook(RendererHookType_t hookType, bool preferSystemLibraries)
{
    RendererHook_t* rendererHook = nullptr;
#ifdef INGAMEOVERLAY_USE_SPDLOG
    SetupSpdLog();
#endif

    switch (hookType)
    {
        case RendererHookType_t::OpenGL:
        {
            auto driver = GetOpenGLDriver(OPENGLX_DLL_NAME);
            if (driver.glXSwapBuffers != nullptr)
            {
                auto hook = OpenGLXHook_t::Inst();
                hook->LibraryName = driver.LibraryPath;
                hook->LoadFunctions(driver.glXSwapBuffers);
                rendererHook = hook;
            }
        }
        break;

        case RendererHookType_t::Vulkan:
        {
        }
        break;
    }

    return rendererHook;
}

}// namespace InGameOverlay