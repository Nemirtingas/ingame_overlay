#include <thread>
#include <mutex>

#include <imgui.h>
#include <ingame_overlay/Renderer_Detector.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#include <thumbs_up.h>

using namespace std::chrono_literals;

static void* g_hModule;

struct overlay_t
{
    std::thread worker;

    ImFontAtlas* font_atlas;
    ingame_overlay::Renderer_Hook* renderer;
    std::mutex overlay_mutex;
    bool show;
    bool stop;
};

static overlay_t* overlay_datas;

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
    overlay_datas = new overlay_t();
    // hmodule is this library HMODULE on Windows   (like if you did call LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you did call dlopen)
    g_hModule = hmodule;

    overlay_datas->worker = std::thread([]()
    {
        std::lock_guard<std::mutex> lk(overlay_datas->overlay_mutex);
        // Try to detect renderer for an infinite amount of time.
        auto future = ingame_overlay::DetectRenderer();
        // Try to detect renderer for at most 4 seconds.
        auto future2 = ingame_overlay::DetectRenderer(4s);
        auto future3 = ingame_overlay::DetectRenderer(4s);
        auto future4 = ingame_overlay::DetectRenderer(4s);
        
        //ingame_overlay::StopRendererDetection();
        std::thread([]() { std::this_thread::sleep_for(20ms); ingame_overlay::DetectRenderer(); }).detach();
        ingame_overlay::FreeDetector();

        future.wait();
        if (future.valid())
        {
            overlay_datas->renderer = future.get();
            if (overlay_datas->renderer == nullptr)
            {
                future = ingame_overlay::DetectRenderer(4s);
                future.wait();
                if (future.valid())
                {
                    overlay_datas->renderer = future.get();
                }

                if (overlay_datas->renderer == nullptr)
                    return;
            }

            // overlay_proc is called  when the process wants to swap buffers.
            overlay_datas->renderer->OverlayProc = []()
            {
                std::lock_guard<std::mutex> lk(overlay_datas->overlay_mutex);
                static char buf[255]{};

                if (!overlay_datas->show)
                    return;

                static std::weak_ptr<uint64_t> image;
                auto sharedImage = image.lock();
                if (sharedImage == nullptr)
                {
                    auto decodedImage = CreateImageFromData(thumbs_up_png, thumbs_up_png_len);

                    image = overlay_datas->renderer->CreateImageResource(decodedImage.Image.data(), decodedImage.Width, decodedImage.Height);

                    sharedImage = image.lock();
                }

                auto& io = ImGui::GetIO();
                float width = io.DisplaySize.x;
                float height = io.DisplaySize.y;

                ImGui::SetNextWindowPos(ImVec2{ 0, 0 });
                ImGui::SetNextWindowSize(ImVec2{ width, height });

                ImGui::SetNextWindowBgAlpha(0.50);

                if (ImGui::Begin("Overlay", &overlay_datas->show, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus))
                {
                    if (!overlay_datas->show)
                    {
                        overlay_datas->renderer->HideAppInputs(false);
                        overlay_datas->renderer->HideOverlayInputs(true);
                    }

                    ImGui::TextUnformatted("Hello from overlay !");
                    ImGui::Text("Mouse pos: %d, %d", (int)io.MousePos.x, (int)io.MousePos.y);
                    ImGui::Text("Renderer Hooked: %s", overlay_datas->renderer->GetLibraryName().c_str());
                    ImGui::InputText("Test input text", buf, sizeof(buf));

                    if (sharedImage != nullptr)
                        ImGui::Image(*sharedImage, { 64, 64 });
                }
                ImGui::End();
            };

            // Called when the renderer hook is ready (true)
            // or when the renderer has been reset (false).
            overlay_datas->renderer->OverlayHookReady = [](bool is_ready)
            {
                std::lock_guard<std::mutex> lk(overlay_datas->overlay_mutex);

                if (!is_ready)
                    overlay_datas->show = false;
            };

            overlay_datas->font_atlas = new ImFontAtlas();

            ImFontConfig fontcfg;

            fontcfg.OversampleH = fontcfg.OversampleV = 1;
            fontcfg.PixelSnapH = true;
            fontcfg.GlyphRanges = overlay_datas->font_atlas->GetGlyphRangesDefault();

            overlay_datas->font_atlas->AddFontDefault(&fontcfg);

            overlay_datas->font_atlas->Build();

            overlay_datas->renderer->StartHook([]()
            {
                std::lock_guard<std::mutex> lk(overlay_datas->overlay_mutex);

                if (overlay_datas->show)
                {
                    overlay_datas->renderer->HideAppInputs(false);
                    overlay_datas->renderer->HideOverlayInputs(true);
                    overlay_datas->show = false;
                }
                else
                {
                    overlay_datas->renderer->HideAppInputs(true);
                    overlay_datas->renderer->HideOverlayInputs(false);
                    overlay_datas->show = true;
                }
            }, { ingame_overlay::ToggleKey::SHIFT, ingame_overlay::ToggleKey::F2 }, overlay_datas->font_atlas);
        }
    });
}

void shared_library_unload(void* hmodule)
{
    {
        std::lock_guard<std::mutex> lk(overlay_datas->overlay_mutex);
        if (overlay_datas->worker.joinable())
            overlay_datas->worker.join();

        overlay_datas->show = false;
        delete overlay_datas->renderer; overlay_datas->renderer = nullptr;
    }
    delete overlay_datas;
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
