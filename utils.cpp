#include "utils.h"

#ifdef __EXPORT_SYMBOLS__
#if defined(UTILS_OS_WINDOWS)

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        shared_library_load(hinstDLL);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        shared_library_unload(hinstDLL);
        break;
    }
    return TRUE;
}

#elif defined(UTILS_OS_LINUX) || defined(UTILS_OS_APPLE) //defined(UTILS_OS_WINDOWS)
#include <dlfcn.h>

__attribute__((constructor)) LOCAL_API void __so_load__()
{
    Dl_info infos;
    dladdr((void*)&__so_load__, &infos);
    shared_library_load(infos.dli_fbase);
}

__attribute__((destructor)) LOCAL_API void __so_unload__()
{
    Dl_info infos;
    dladdr((void*)&__so_load__, &infos);
    shared_library_unload(infos.dli_fbase);
}

#endif//defined(UTILS_OS_LINUX) || defined(UTILS_OS_APPLE)
#endif//defined(__EXPORT_SYMBOLS__)