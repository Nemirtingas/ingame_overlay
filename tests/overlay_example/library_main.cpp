#include <thread>
#include <mutex>
#include <vector>

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../common/stb_image.h"
#include "../common/thumbs_up.h"
#include "../common/thumbs_down.h"
#include "../common/thumbs_down_small.h"
#include "../common/right_facing_fist.h"

#define INGAMEOVERLAY_TEST_BATCH_RESOURCE_LOAD 1
#define INGAMEOVERLAY_TEST_ONDEMAND_RESOURCE_LOAD 0

using namespace std::chrono_literals;

template<typename T, size_t N>
constexpr size_t CountOf(T(&)[N])
{
    return N;
}

struct Image
{
    int32_t Width;
    int32_t Height;
    std::vector<uint32_t> Image;
};

struct OverlayData_t
{
    std::thread Worker;

    InGameOverlay::ToggleKey ToggleKeys[2] = { InGameOverlay::ToggleKey::SHIFT, InGameOverlay::ToggleKey::F2 };
    ImFontAtlas* FontAtlas = nullptr;
    InGameOverlay::RendererHook_t* Renderer = nullptr;
    InGameOverlay::RendererResource_t* OverlayImage1 = nullptr;
    InGameOverlay::RendererResource_t* OverlayImage2 = nullptr;
    std::chrono::system_clock::time_point ImageTimer;
    bool ThumbUpSelected;
    Image ThumbsUp;
    Image ThumbsDown;
    Image RightFacingFist;
    std::recursive_mutex OverlayMutex;
    char OverlayInputTextBuffer[256]{};
    bool Show;
    bool Stop;
};

static OverlayData_t* OverlayData;

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
            future = InGameOverlay::DetectRenderer(8s);
            future.wait();
            if (future.valid())
                rendererHook = future.get();
        }

        InGameOverlay::FreeDetector();
    }

    return rendererHook;
}

InGameOverlay::RendererHook_t* test_filterer_renderer_detector(InGameOverlay::RendererHookType_t hookType, bool preferSystemLibraries)
{
    InGameOverlay::RendererHook_t* rendererHook = nullptr;

    auto future = InGameOverlay::DetectRenderer(8s, hookType, preferSystemLibraries);
    future.wait();
    if (future.valid())
        rendererHook = future.get();

    InGameOverlay::FreeDetector();

    return rendererHook;
}

void shared_library_load(void* hmodule)
{
    OverlayData = new OverlayData_t();
    // hmodule is this library HMODULE on Windows   (like if you did call LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you did call dlopen)

    OverlayData->Worker = std::thread([]()
    {
        //std::this_thread::sleep_for(5s);

        std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);

        //OverlayData->Renderer = test_renderer_detector();
        OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::Any, false);
        //OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::OpenGL | InGameOverlay::RendererHookType_t::DirectX11 | InGameOverlay::RendererHookType_t::DirectX12);
        if (OverlayData->Renderer == nullptr)
            return;

        // FIXME: Cannot hook DXVK's DirectX11 device's ID3D11DeviceRelease
        if (OverlayData->Renderer->GetRendererHookType() == InGameOverlay::RendererHookType_t::DirectX11 && OverlayData->Renderer->GetLibraryName().find("(DXVK)") != std::string::npos)
        {
            delete OverlayData->Renderer;
            OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::Vulkan, false);
        }

        // overlay_proc is called  when the process wants to swap buffers.
        OverlayData->Renderer->OverlayProc = []()
        {
            std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);

            if (!OverlayData->Show)
                return;

            if (OverlayData->OverlayImage1 == nullptr)
            {
                // When auto load is used, you need to keep the data alive, the resource will not keep ownership or make a copy
                OverlayData->ThumbsUp = CreateImageFromData(thumbs_up_png, thumbs_up_png_len);
                OverlayData->ThumbsDown = CreateImageFromData(thumbs_down_small_png, thumbs_down_small_png_len);
                OverlayData->RightFacingFist = CreateImageFromData(right_facing_fist_png, right_facing_fist_png_len);

                OverlayData->OverlayImage1 = OverlayData->Renderer->CreateResource();
                OverlayData->OverlayImage2 = OverlayData->Renderer->CreateResource();

                OverlayData->ImageTimer = std::chrono::system_clock::now();
                OverlayData->ThumbUpSelected = true;

#if INGAMEOVERLAY_TEST_BATCH_RESOURCE_LOAD
                OverlayData->OverlayImage1->AttachResource(OverlayData->ThumbsUp.Image.data(), OverlayData->ThumbsUp.Width, OverlayData->ThumbsUp.Height);
                OverlayData->OverlayImage2->AttachResource(OverlayData->ThumbsDown.Image.data(), OverlayData->ThumbsDown.Width, OverlayData->ThumbsDown.Height);
#elif INGAMEOVERLAY_TEST_ONDEMAND_RESOURCE_LOAD
                // Set here the AutoLoad because by default, the RendererHook uses Batch auto load. Could also use RendererHook_t::SetResourceAutoLoad(InGameOverlay::ResourceAutoLoad_t::OnUse)
                OverlayData->OverlayImage1->SetAutoLoad(InGameOverlay::ResourceAutoLoad_t::OnUse);
                OverlayData->OverlayImage2->SetAutoLoad(InGameOverlay::ResourceAutoLoad_t::OnUse);

                OverlayData->OverlayImage1->AttachResource(OverlayData->ThumbsUp.Image.data(), OverlayData->ThumbsUp.Width, OverlayData->ThumbsUp.Height);
                OverlayData->OverlayImage2->AttachResource(OverlayData->ThumbsDown.Image.data(), OverlayData->ThumbsDown.Width, OverlayData->ThumbsDown.Height);
#else
                OverlayData->OverlayImage1->SetAutoLoad(InGameOverlay::ResourceAutoLoad_t::None);
                OverlayData->OverlayImage2->SetAutoLoad(InGameOverlay::ResourceAutoLoad_t::None);

                OverlayData->OverlayImage1->Load(OverlayData->ThumbsUp.Image.data(), OverlayData->ThumbsUp.Width, OverlayData->ThumbsUp.Height);
                OverlayData->OverlayImage2->Load(OverlayData->ThumbsDown.Image.data(), OverlayData->ThumbsDown.Width, OverlayData->ThumbsDown.Height);
#endif
            }
            
            if ((std::chrono::system_clock::now() - OverlayData->ImageTimer) > 1s)
            {
                // This is NOT the way to switch images, is it GPU costly and inefficient.
                // Doing someting like this will unload the image from the GPU, and then upload the other buffer onto the GPU.
                // You should load often used images once in your GPU and then switch the ImGui's image id in the code instead.
                // But for the autoload feature example's sack, we do it the wrong way.
                if (OverlayData->ThumbUpSelected)
                {
                    OverlayData->OverlayImage1->AttachResource(OverlayData->RightFacingFist.Image.data(), OverlayData->RightFacingFist.Width, OverlayData->RightFacingFist.Height);
                    OverlayData->OverlayImage2->AttachResource(OverlayData->RightFacingFist.Image.data(), OverlayData->RightFacingFist.Width, OverlayData->RightFacingFist.Height);
                }
                else
                {
                    OverlayData->OverlayImage1->AttachResource(OverlayData->ThumbsUp.Image.data(), OverlayData->ThumbsUp.Width, OverlayData->ThumbsUp.Height);
                    OverlayData->OverlayImage2->AttachResource(OverlayData->ThumbsDown.Image.data(), OverlayData->ThumbsDown.Width, OverlayData->ThumbsDown.Height);
                }

                OverlayData->ImageTimer = std::chrono::system_clock::now();
                OverlayData->ThumbUpSelected = !OverlayData->ThumbUpSelected;
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
                ImGui::Text("Renderer Hooked: %s", OverlayData->Renderer->GetLibraryName());
                ImGui::InputText("Test input text", OverlayData->OverlayInputTextBuffer, sizeof(OverlayData->OverlayInputTextBuffer));

                // Good habit is to use a dummy when the image is not ready, to not screw up your layout
                if (OverlayData->OverlayImage1->GetResourceId() != 0)
                    ImGui::Image(OverlayData->OverlayImage1->GetResourceId(), { 64, 64 });
                else
                    ImGui::Dummy({ 64, 64 });

                ImGui::SameLine();

                // Good habit is to use a dummy when the image is not ready, to not screw up your layout
                if (OverlayData->OverlayImage2->GetResourceId() != 0)
                    ImGui::Image(OverlayData->OverlayImage2->GetResourceId(), ImVec2(OverlayData->OverlayImage2->Width(), OverlayData->OverlayImage2->Height()));
                else
                    ImGui::Dummy(ImVec2(OverlayData->OverlayImage2->Width(), OverlayData->OverlayImage2->Height()));
            }
            ImGui::End();
        };

        // Called on Renderer hook status change
        OverlayData->Renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState hookState)
        {
            std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);

            if (hookState == InGameOverlay::OverlayHookState::Removing)
            {
                if (OverlayData->OverlayImage1 != nullptr) OverlayData->OverlayImage1->Delete();
                if (OverlayData->OverlayImage2 != nullptr) OverlayData->OverlayImage2->Delete();
                OverlayData->Show = false;
            }
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
            std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);
            auto& io = ImGui::GetIO();

            if (OverlayData->Show)
            {
                OverlayData->Renderer->HideAppInputs(false);
                OverlayData->Renderer->HideOverlayInputs(true);
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                io.MouseDrawCursor = false;
                OverlayData->Show = false;
            }
            else
            {
                OverlayData->Renderer->HideAppInputs(true);
                OverlayData->Renderer->HideOverlayInputs(false);
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
                io.MouseDrawCursor = true;
                OverlayData->Show = true;
            }
        }, OverlayData->ToggleKeys, CountOf(OverlayData->ToggleKeys), OverlayData->FontAtlas);
    });
}

void shared_library_unload(void* hmodule)
{
    {
        std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);
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
