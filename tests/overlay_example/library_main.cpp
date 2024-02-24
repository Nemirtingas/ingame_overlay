#include <thread>
#include <mutex>
#include <vector>

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#include <thumbs_up.h>

using namespace std::chrono_literals;

struct OverlayData_t
{
    std::thread Worker;

    ImFontAtlas* FontAtlas;
    InGameOverlay::RendererHook_t* Renderer;
    std::mutex OverlayMutex;
    char OverlayInputTextBuffer[256]{};
    bool Show;
    bool Stop;
};

static OverlayData_t* OverlayData;

struct Image
{
    int32_t Width;
    int32_t Height;
    std::vector<uint32_t> Image;
};

Image CreateImageFromData(void const* data, size_t data_len)
{
    Image res;
    int32_t width, height;

    stbi_uc* buffer = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(data), data_len, &width, &height, nullptr, 4);
    if (buffer == nullptr)
        return res;

    res.Image.resize(size_t(width) * size_t(height));
    res.Width = width;
    res.Height = height;
    memcpy(res.Image.data(), buffer, res.Image.size() * sizeof(uint32_t));

    stbi_image_free(buffer);

    return res;
}

void shared_library_load(void* hmodule)
{
    OverlayData = new OverlayData_t();
    // hmodule is this library HMODULE on Windows   (like if you did call LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you did call dlopen)

    OverlayData->Worker = std::thread([]()
    {
        std::lock_guard<std::mutex> lk(OverlayData->OverlayMutex);
        // Try to detect Renderer for an infinite amount of time.
        auto future = InGameOverlay::DetectRenderer();
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
            OverlayData->Renderer = future.get();
            if (OverlayData->Renderer == nullptr)
            {
                future = InGameOverlay::DetectRenderer(4s);
                future.wait();
                if (future.valid())
                {
                    OverlayData->Renderer = future.get();
                }

                if (OverlayData->Renderer == nullptr)
                    return;
            }

            // overlay_proc is called  when the process wants to swap buffers.
            OverlayData->Renderer->OverlayProc = []()
            {
                std::lock_guard<std::mutex> lk(OverlayData->OverlayMutex);

                if (!OverlayData->Show)
                    return;

                static std::weak_ptr<uint64_t> image;
                auto sharedImage = image.lock();
                if (sharedImage == nullptr)
                {
                    auto decodedImage = CreateImageFromData(thumbs_up_png, thumbs_up_png_len);

                    image = OverlayData->Renderer->CreateImageResource(decodedImage.Image.data(), decodedImage.Width, decodedImage.Height);

                    sharedImage = image.lock();
                }

                auto& io = ImGui::GetIO();
                float width = io.DisplaySize.x;
                float height = io.DisplaySize.y;

                ImGui::SetNextWindowPos(ImVec2{ 0, 0 });
                ImGui::SetNextWindowSize(ImVec2{ width, height });

                ImGui::SetNextWindowBgAlpha(0.50);

                if (ImGui::Begin("Overlay", &OverlayData->Show, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus))
                {
                    if (!OverlayData->Show)
                    {
                        OverlayData->Renderer->HideAppInputs(false);
                        OverlayData->Renderer->HideOverlayInputs(true);
                    }

                    ImGui::TextUnformatted("Hello from overlay !");
                    ImGui::Text("Mouse pos: %d, %d", (int)io.MousePos.x, (int)io.MousePos.y);
                    ImGui::Text("Renderer Hooked: %s", OverlayData->Renderer->GetLibraryName().c_str());
                    ImGui::InputText("Test input text", OverlayData->OverlayInputTextBuffer, sizeof(OverlayData->OverlayInputTextBuffer));

                    if (sharedImage != nullptr)
                        ImGui::Image(*sharedImage, { 64, 64 });
                }
                ImGui::End();
            };

            // Called on Renderer hook status change
            OverlayData->Renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState hookState)
            {
                std::lock_guard<std::mutex> lk(OverlayData->OverlayMutex);

                if (hookState == InGameOverlay::OverlayHookState::Removing)
                    OverlayData->Show = false;
            };

            OverlayData->FontAtlas = new ImFontAtlas();

            ImFontConfig fontcfg;

            fontcfg.OversampleH = fontcfg.OversampleV = 1;
            fontcfg.PixelSnapH = true;
            fontcfg.GlyphRanges = OverlayData->FontAtlas->GetGlyphRangesDefault();

            OverlayData->FontAtlas->AddFontDefault(&fontcfg);

            OverlayData->FontAtlas->Build();

            OverlayData->Renderer->StartHook([]()
            {
                std::lock_guard<std::mutex> lk(OverlayData->OverlayMutex);

                if (OverlayData->Show)
                {
                    OverlayData->Renderer->HideAppInputs(false);
                    OverlayData->Renderer->HideOverlayInputs(true);
                    OverlayData->Show = false;
                }
                else
                {
                    OverlayData->Renderer->HideAppInputs(true);
                    OverlayData->Renderer->HideOverlayInputs(false);
                    OverlayData->Show = true;
                }
            }, { InGameOverlay::ToggleKey::SHIFT, InGameOverlay::ToggleKey::F2 }, OverlayData->FontAtlas);
        }
    });
}

void shared_library_unload(void* hmodule)
{
    {
        std::lock_guard<std::mutex> lk(OverlayData->OverlayMutex);
        if (OverlayData->Worker.joinable())
            OverlayData->Worker.join();

        OverlayData->Show = false;
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
