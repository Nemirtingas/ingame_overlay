#include "utils.h"

#include <thread>
#include <mutex>

#include "Renderer_Detector.h"

#include <imgui.h>

static void* g_hModule;

struct overlay_t
{
    std::thread worker;

    Renderer_Hook* renderer;
    std::mutex overlay_mutex;
    bool show;
    bool stop;
} overlay_datas;

void LOCAL_API shared_library_load(void* hmodule)
{
    // hmodule is this library HMODULE on Windows   (like if you did call LoadLibrary)
    // hmodule is this library void* on Linux/MacOS (like if you did cal dlopen)
    g_hModule = hmodule;

    overlay_datas.worker = std::thread([]()
    {
        std::lock_guard<std::mutex> lk(overlay_datas.overlay_mutex);
        // Try to detect renderer for at least 15 seconds.
        auto future = detect_renderer(std::chrono::milliseconds{ 15000 });

        future.wait();
        if (future.valid())
        {
            overlay_datas.renderer = future.get();
            if(overlay_datas.renderer == nullptr)
                return;

            // overlay_proc is called  when the process wants to swap buffers.
            overlay_datas.renderer->overlay_proc = []()
            {
                std::lock_guard<std::mutex> lk(overlay_datas.overlay_mutex);

                if (!overlay_datas.show)
                    return;

                auto& io = ImGui::GetIO();
                float width = io.DisplaySize.x;
                float height = io.DisplaySize.y;

                ImGui::SetNextWindowPos(ImVec2{ 0, 0 });
                ImGui::SetNextWindowSize(ImVec2{ width, height });

                ImGui::SetNextWindowBgAlpha(0.50);

                if (ImGui::Begin("Overlay", &overlay_datas.show, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus))
                {
                    ImGui::TextUnformatted("Hello from overlay !");
                    ImGui::Text("%d, %d", (int)io.MousePos.x, (int)io.MousePos.y);
                    ImGui::Text("Renderer Hooked: %s", overlay_datas.renderer->get_lib_name());
                }
                ImGui::End();
            };

            // Called when the renderer hook is ready (true)
            // or when the renderer has been reset (false).
            overlay_datas.renderer->overlay_hook_ready = [](bool is_ready)
            {
                std::lock_guard<std::mutex> lk(overlay_datas.overlay_mutex);
            };

            overlay_datas.renderer->start_hook([](bool toggle)
            {
                std::lock_guard<std::mutex> lk(overlay_datas.overlay_mutex);

                // toggle is true when the key combination to open the overlay has been pressed
                if(toggle)
                    overlay_datas.show = !overlay_datas.show;

                // return the state of the overlay:
                //  false = overlay is hidden
                //  true = overlay is shown
                return overlay_datas.show;
            });
        }
    });
}

void LOCAL_API shared_library_unload(void* hmodule)
{
    std::lock_guard<std::mutex> lk(overlay_datas.overlay_mutex);
    overlay_datas.worker.detach();
    overlay_datas.show = false;
    delete overlay_datas.renderer;
}
