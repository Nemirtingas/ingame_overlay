#include <thread>
#include <mutex>
#include <vector>

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

static OverlayData_t* OverlayData;

InGameOverlay::RendererHook_t* test_renderer_detector()
{
    InGameOverlay::RendererHook_t* rendererHook = nullptr;
    // Try to detect Renderer for an infinite amount of time.
    auto future = InGameOverlay::DetectRenderer();
    InGameOverlay::StopRendererDetection();
    // Try to detect Renderer for at most 4 seconds.
    auto future2 = InGameOverlay::DetectRenderer(4s);
    auto future3 = InGameOverlay::DetectRenderer(4s);
    auto future4 = InGameOverlay::DetectRenderer(4s);

    //InGameOverlay::StopRendererDetection();
    std::thread([]() { std::this_thread::sleep_for(20ms); InGameOverlay::DetectRenderer(); }).detach();
    InGameOverlay::FreeDetector();

    future.wait();
    if (future.valid())
    {
        rendererHook = future.get();
        if (rendererHook == nullptr)
        {
            future = InGameOverlay::DetectRenderer(4s);
            future.wait();
            if (future.valid())
                rendererHook = future.get();
        }

        InGameOverlay::FreeDetector();
    }

    return rendererHook;
}

InGameOverlay::RendererHook_t* test_fixed_renderer(InGameOverlay::RendererHookType_t hookType)
{
    return InGameOverlay::CreateRendererHook(hookType, true);
}

void shared_library_load(void* hmodule)
{
    OverlayData = new OverlayData_t();
    // hmodule is this library HMODULE on Windows   (like if you did call LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you did call dlopen)

    OverlayData->Worker = std::thread([]()
    {
        std::this_thread::sleep_for(5s);

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

        OverlayData->FontAtlas->Build();

        OverlayData->Renderer->StartHook([](){}, { InGameOverlay::ToggleKey::SHIFT, InGameOverlay::ToggleKey::F2 }, OverlayData->FontAtlas);
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
