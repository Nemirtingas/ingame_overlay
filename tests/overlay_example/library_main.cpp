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

#define INGAMEOVERLAY_TEST_BATCH_RESOURCE_LOAD 0
#define INGAMEOVERLAY_TEST_ONDEMAND_RESOURCE_LOAD 1

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
    std::vector<uint8_t> Image;
};

struct OverlayData_t
{
    std::thread Worker;

    InGameOverlay::ToggleKey ToggleKeys[2] = { InGameOverlay::ToggleKey::SHIFT, InGameOverlay::ToggleKey::F2 };
    ImFontAtlas* FontAtlas = nullptr;
    InGameOverlay::RendererHook_t* Renderer = nullptr;
    InGameOverlay::RendererResource_t* OverlayImage1 = nullptr;
    InGameOverlay::RendererResource_t* OverlayImage2 = nullptr;
    InGameOverlay::RendererResource_t* OverlayImageScreenshot = nullptr;
    std::chrono::system_clock::time_point ImageTimer;
    bool ThumbUpSelected;
    Image ThumbsUp;
    Image ThumbsDown;
    Image RightFacingFist;
    Image Screenshot;
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

    res.Image.resize(size_t(width) * size_t(height) * 4);
    res.Width = width;
    res.Height = height;
    memcpy(res.Image.data(), buffer, res.Image.size());

    stbi_image_free(buffer);

    return res;
}

static float Float16ToFloat(uint16_t h)
{
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;

    uint32_t f;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            f = (sign << 31); // Zero
        }
        else
        {
            // Subnormal -> normalize
            exponent = 1;
            while ((mantissa & 0x400) == 0)
            {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FF;
            exponent += 127 - 15;
            f = (sign << 31) | (exponent << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 0x1F)
    {
        // Inf or NaN
        f = (sign << 31) | (0xFF << 23) | (mantissa << 13);
    }
    else
    {
        exponent = exponent - 15 + 127;
        f = (sign << 31) | (exponent << 23) | (mantissa << 13);
    }

    float result;
    memcpy(&result, &f, sizeof(result));
    return result;
}

static void write_pixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a;
}

static void ConvertR8G8B8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t r = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t b = currentPixelBuffer[2];
    write_pixel(destination, r, g, b, 255);
}

static void ConvertB8G8R8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t b = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t r = currentPixelBuffer[2];
    write_pixel(destination, r, g, b, 255);
}

static void ConvertR8G8B8A8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t r = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t b = currentPixelBuffer[2];
    uint8_t a = currentPixelBuffer[3];
    write_pixel(destination, r, g, b, 255);
}

static void ConvertX8R8G8B8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t b = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t r = currentPixelBuffer[2];
    write_pixel(destination, r, g, b, 255);
}

static void ConvertA8R8G8B8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t b = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t r = currentPixelBuffer[2];
    uint8_t a = currentPixelBuffer[3];
    write_pixel(destination, r, g, b, 255);
}

static void ConvertB8G8R8A8ToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    uint8_t b = currentPixelBuffer[0];
    uint8_t g = currentPixelBuffer[1];
    uint8_t r = currentPixelBuffer[2];
    uint8_t a = currentPixelBuffer[3];
    write_pixel(destination, r, g, b, 255);
}

#define ConvertB8G8R8X8ToRGBA ConvertB8G8R8ToRGBA

static void ConvertR16G16B16A16FloatToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    const uint16_t* fp = reinterpret_cast<const uint16_t*>(currentPixelBuffer);
    auto f16_to_u8 = [](uint16_t half) -> uint8_t {
        return static_cast<uint8_t>(std::min(255.f, std::max(0.f, Float16ToFloat(half) * 255.f)));
    };
    write_pixel(destination,
        f16_to_u8(fp[0]),
        f16_to_u8(fp[1]),
        f16_to_u8(fp[2]),
        255);
}

static void ConvertR16G16B16A16UnormToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    const uint16_t* up = reinterpret_cast<const uint16_t*>(currentPixelBuffer);
    write_pixel(destination, 
        static_cast<uint8_t>((up[0] * 255) / 65535),
        static_cast<uint8_t>((up[1] * 255) / 65535),
        static_cast<uint8_t>((up[2] * 255) / 65535),
        255);
}

static void ConvertR32G32B32A32FloatToRGBA(uint8_t* destination, const uint8_t* currentPixelBuffer)
{
    const float* fp = reinterpret_cast<const float*>(currentPixelBuffer);
    write_pixel(destination,
        static_cast<uint8_t>(std::clamp(fp[0] * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(fp[1] * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(fp[2] * 255.0f, 0.0f, 255.0f)),
        255);
}

static bool ConvertToRGBA8888(const InGameOverlay::ScreenshotCallbackParameter_t* input, std::vector<uint8_t>& outRGBA)
{
    const uint32_t width = input->Width;
    const uint32_t height = input->Height;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(input->Data);

    void(*convertFunc)(uint8_t*, const uint8_t*) = nullptr;

    switch (input->Format)
    {
        case InGameOverlay::ScreenshotDataFormat_t::R8G8B8             : convertFunc = ConvertR8G8B8ToRGBA           ; break;
        case InGameOverlay::ScreenshotDataFormat_t::X8R8G8B8           : convertFunc = ConvertX8R8G8B8ToRGBA         ; break;
        case InGameOverlay::ScreenshotDataFormat_t::A8R8G8B8           : convertFunc = ConvertA8R8G8B8ToRGBA         ; break;
        case InGameOverlay::ScreenshotDataFormat_t::B8G8R8A8           : convertFunc = ConvertB8G8R8A8ToRGBA         ; break;
        case InGameOverlay::ScreenshotDataFormat_t::B8G8R8X8           : convertFunc = ConvertB8G8R8X8ToRGBA         ; break;
        case InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8           : convertFunc = ConvertR8G8B8A8ToRGBA         ; break;
        case InGameOverlay::ScreenshotDataFormat_t::A2R10G10B10        : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::A2B10G10R10        : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::R10G10B10A2        : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::R5G6B5             : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::X1R5G5B5           : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::A1R5G5B5           : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::B5G6R5             : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::B5G5R5A1           : convertFunc = nullptr                       ; break;
        case InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_FLOAT : convertFunc = ConvertR16G16B16A16FloatToRGBA; break;
        case InGameOverlay::ScreenshotDataFormat_t::R16G16B16A16_UNORM : convertFunc = ConvertR16G16B16A16UnormToRGBA; break;
        case InGameOverlay::ScreenshotDataFormat_t::R32G32B32A32_FLOAT : convertFunc = ConvertR32G32B32A32FloatToRGBA; break;
    }

    if (convertFunc == nullptr)
        return false;

    outRGBA.resize(width * height * 4);
    for (uint32_t i = 0; i < height; ++i)
    {
        for (uint32_t j = 0; j < width; ++j)
        {
            convertFunc(outRGBA.data() + (j + i * width) * 4, src + i * input->Pitch + (j * input->PixelSize));
        }
    }

    return true;
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

void image_or_dummy(InGameOverlay::RendererResource_t* resource, float width, float height)
{
    if (resource->GetResourceId() != 0)
        ImGui::Image(resource->GetResourceId(), { width, height });
    else
        ImGui::Dummy({ width, height });
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
        //OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::Vulkan, false);
        OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::AnyDirectX, false);
        //OverlayData->Renderer = test_filterer_renderer_detector(InGameOverlay::RendererHookType_t::OpenGL | InGameOverlay::RendererHookType_t::DirectX11 | InGameOverlay::RendererHookType_t::DirectX12, false);
        if (OverlayData->Renderer == nullptr)
            return;

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
                // But for the autoload feature example's sake, we do it the wrong way.
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
                ImGui::SameLine();
                if (ImGui::Button("Take screenshot before overlay"))
                    OverlayData->Renderer->TakeScreenshot(InGameOverlay::ScreenshotType_t::BeforeOverlay);

                ImGui::SameLine();
                if (ImGui::Button("Take screenshot after overlay"))
                    OverlayData->Renderer->TakeScreenshot(InGameOverlay::ScreenshotType_t::AfterOverlay);

                ImGui::Text("Mouse pos: %d, %d", (int)io.MousePos.x, (int)io.MousePos.y);
                ImGui::Text("Renderer Hooked: %s", OverlayData->Renderer->GetLibraryName());
                ImGui::InputText("Test input text", OverlayData->OverlayInputTextBuffer, sizeof(OverlayData->OverlayInputTextBuffer));

                // Good habit is to use a dummy when the image is not ready, to not screw up your layout
                image_or_dummy(OverlayData->OverlayImage1, 64, 64);
                
                ImGui::SameLine();
                
                // Good habit is to use a dummy when the image is not ready, to not screw up your layout
                image_or_dummy(OverlayData->OverlayImage2, 64, 64);

                if (OverlayData->OverlayImageScreenshot)
                {
                    bool opened = true;
                    if (ImGui::Begin("Screenshot window", &opened, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        if (!opened)
                        {
                            if (OverlayData->OverlayImageScreenshot)
                            {
                                OverlayData->OverlayImageScreenshot->Delete();
                                OverlayData->OverlayImageScreenshot = nullptr;
                            }
                        }
                        else
                        {
                            image_or_dummy(OverlayData->OverlayImageScreenshot, OverlayData->OverlayImageScreenshot->Width() / 2, OverlayData->OverlayImageScreenshot->Height() / 2);
                        }
                    }
                    ImGui::End();
                }
            }
            ImGui::End();
        };

        // Called on Renderer hook status change
        OverlayData->Renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState hookState)
        {
            std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);

            if (hookState == InGameOverlay::OverlayHookState::Removing)
            {
                if (OverlayData->OverlayImage1 != nullptr && OverlayData->OverlayImage1->GetResourceId() != 0)
                    OverlayData->OverlayImage1->Unload();

                if (OverlayData->OverlayImage2 != nullptr && OverlayData->OverlayImage2->GetResourceId() != 0)
                    OverlayData->OverlayImage2->Unload();

                if (OverlayData->OverlayImageScreenshot != nullptr && OverlayData->OverlayImageScreenshot->GetResourceId() != 0)
                    OverlayData->OverlayImageScreenshot->Unload();

                if (OverlayData->OverlayImage1 != nullptr)
                {
                    OverlayData->OverlayImage1->Delete();
                    OverlayData->OverlayImage1 = nullptr;
                }
                if (OverlayData->OverlayImage2 != nullptr)
                {
                    OverlayData->OverlayImage2->Delete();
                    OverlayData->OverlayImage2 = nullptr;
                }
                if (OverlayData->OverlayImageScreenshot != nullptr)
                {
                    OverlayData->OverlayImageScreenshot->Delete();
                    OverlayData->OverlayImageScreenshot = nullptr;
                }
                OverlayData->Show = false;
            }
        };

        OverlayData->FontAtlas = new ImFontAtlas();

        ImFontConfig fontcfg;

        fontcfg.OversampleH = fontcfg.OversampleV = 1;
        fontcfg.PixelSnapH = true;
        fontcfg.GlyphRanges = OverlayData->FontAtlas->GetGlyphRangesDefault();

        OverlayData->FontAtlas->AddFontDefault(&fontcfg);

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

        OverlayData->Renderer->SetScreenshotCallback([](InGameOverlay::ScreenshotCallbackParameter_t const* screenshot, void* userParam)
        {
            if (OverlayData->OverlayImageScreenshot)
            {
                OverlayData->OverlayImageScreenshot->Delete();
            }

            OverlayData->Screenshot.Width = screenshot->Width;
            OverlayData->Screenshot.Height = screenshot->Height;
            if (ConvertToRGBA8888(screenshot, OverlayData->Screenshot.Image))
                OverlayData->OverlayImageScreenshot = OverlayData->Renderer->CreateAndLoadResource(OverlayData->Screenshot.Image.data(), OverlayData->Screenshot.Width, OverlayData->Screenshot.Height, true);
        }, nullptr);
    });
}

void shared_library_unload(void* hmodule)
{
    {
        std::lock_guard<std::recursive_mutex> lk(OverlayData->OverlayMutex);
        if (OverlayData->Worker.joinable())
            OverlayData->Worker.join();

        OverlayData->Show = false;

        if (OverlayData->OverlayImage1 != nullptr && OverlayData->OverlayImage1->GetResourceId() != 0)
            OverlayData->OverlayImage1->Unload();

        if (OverlayData->OverlayImage2 != nullptr && OverlayData->OverlayImage2->GetResourceId() != 0)
            OverlayData->OverlayImage2->Unload();

        if (OverlayData->OverlayImageScreenshot != nullptr && OverlayData->OverlayImageScreenshot->GetResourceId() != 0)
            OverlayData->OverlayImageScreenshot->Unload();

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
