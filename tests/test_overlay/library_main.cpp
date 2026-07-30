#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <iostream>

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

using namespace std::chrono_literals;

struct OverlayData_t
{
    std::thread Worker;

    ImFontAtlas* FontAtlas;
    InGameOverlay::RendererHook_t* Renderer;
    std::recursive_mutex OverlayMutex;
};

static InGameOverlay::ToggleKey OverlayToggleKeys[] = { InGameOverlay::ToggleKey::SHIFT, InGameOverlay::ToggleKey::F2 };

static OverlayData_t* OverlayData;

InGameOverlay::RendererHook_t* test_renderer_detector(bool autoDetection = true, InGameOverlay::RendererHookType_t rendererFilter = InGameOverlay::RendererHookType_t::Any)
{
    InGameOverlay::RendererHook_t* rendererHook = nullptr;

    if (autoDetection)
    {
        // Try to detect Renderer for an infinite amount of time.
        auto continueDetection = InGameOverlay::DetectRenderer();
        InGameOverlay::StopRendererDetection();
        // InGameOverlay::FreeDetector();

        // Choose your expiration time
        auto expirationTime = std::chrono::steady_clock::now() + 8s;

        while (std::chrono::steady_clock::now() < expirationTime && InGameOverlay::DetectRenderer(true, rendererFilter))
        {
            // Run the detection code (.dll and functions hooking) at your pace
            std::this_thread::sleep_for(20ms);
        }

        rendererHook = InGameOverlay::GetDetectedRenderer();

        // You can keep the detector for future usage.
        InGameOverlay::FreeDetector();
    }
    else
    {
        rendererHook = InGameOverlay::GetRenderer(rendererFilter, true);
    }

    return rendererHook;
}

void shared_library_load(void* hmodule)
{
    OverlayData = new OverlayData_t();
    // hmodule is this library HMODULE on Windows   (like if you called LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you called dlopen)

    OverlayData->Worker = std::thread([]()
    {
        std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);

        OverlayData->Renderer = test_renderer_detector();
        if (OverlayData->Renderer == nullptr)
        {
            exit(-1);
            return;
        }

        // overlay_proc is called  when the process wants to swap buffers.
        OverlayData->Renderer->OverlayProc = []()
        {
            exit(0);
        };

        // Called on Renderer hook status change
        OverlayData->Renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState hookState)
        {
        };

        OverlayData->FontAtlas = new ImFontAtlas();

        ImFontConfig fontcfg;

        fontcfg.OversampleH = fontcfg.OversampleV = 1;
        fontcfg.PixelSnapH = true;
        fontcfg.GlyphRanges = OverlayData->FontAtlas->GetGlyphRangesDefault();

        OverlayData->FontAtlas->AddFontDefault(&fontcfg);

        OverlayData->Renderer->StartHook([](){}, OverlayToggleKeys, 2, OverlayData->FontAtlas);
    });
}

void shared_library_unload(void* hmodule)
{
    {
        std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);
        if (OverlayData->Worker.joinable())
            OverlayData->Worker.join();

        delete OverlayData->Renderer; OverlayData->Renderer = nullptr;
    }
    delete OverlayData;
}

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) ||\
    defined(_WIN64) || defined(WIN64) || defined(__MINGW64__)

#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch( fdwReason )
    {
        case DLL_PROCESS_ATTACH:
            shared_library_load((void*)hinstDLL);
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            shared_library_unload((void*)hinstDLL);
            break;
    }
    return TRUE;
}

#else
#include <dlfcn.h>

__attribute__((constructor)) void library_constructor()
{
    Dl_info infos;
    dladdr((void*)&library_constructor, &infos);
    shared_library_load(infos.dli_fbase);
}

__attribute__((destructor)) void library_destructor()
{
    Dl_info infos;
    dladdr((void*)&library_constructor, &infos);
    shared_library_unload(infos.dli_fbase);
}

#endif
