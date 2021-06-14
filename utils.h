#pragma once

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__)
    #define UTILS_OS_WINDOWS
    #define UTILS_ARCH_X64
#elif defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
    #define UTILS_OS_WINDOWS
    #define UTILS_ARCH_X86
#elif defined(__linux__) || defined(linux)
    #if defined(__x86_64__)
        #define UTILS_OS_LINUX
        #define UTILS_ARCH_X64
    #else
        #define UTILS_OS_LINUX
        #define UTILS_ARCH_X86
    #endif
#elif defined(__APPLE__)
    #if defined(__x86_64__)
        #define UTILS_OS_APPLE
        #define UTILS_ARCH_X64
    #else
        #define UTILS_OS_APPLE
        #define UTILS_ARCH_X86
    #endif
#else
    //#error "Unknown OS"
#endif

#ifdef __cplusplus
    #define EXPORT_C_API   extern "C"
#else
    #define EXPORT_C_API   extern
#endif
#define EXPORT_CXX_API extern

#if defined(UTILS_OS_WINDOWS)

    #define EXPORT_API(mode) __declspec(mode)
    #define EXPORT_STATIC_API

    #define LOCAL_API

#elif defined(UTILS_OS_LINUX) || defined(UTILS_OS_APPLE)

    #define EXPORT_API(mode) __attribute__((visibility ("default")))
    #define EXPORT_STATIC_API EXPORT_API(static)

    //#define LOCAL_API __attribute__((visibility ("internal")))
    #define LOCAL_API __attribute__((visibility ("hidden")))

#endif

#ifdef __EXPORT_SYMBOLS__
void LOCAL_API shared_library_load(void* hmodule);
void LOCAL_API shared_library_unload(void* hmodule);
#endif