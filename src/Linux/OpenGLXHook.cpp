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

#include <glad/gl.h>

#include "OpenGLXHook.h"
#include "X11Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

namespace InGameOverlay {

#define TRY_HOOK_FUNCTION_OR_FAIL(NAME) do { if (!HookFunc(std::make_pair<void**, void*>(&(void*&)_##NAME, (void*)&OpenGLXHook_t::_My##NAME))) { \
    INGAMEOVERLAY_ERROR("Failed to hook {}", #NAME);\
    UnhookAll();\
    return false;\
} } while(0)

OpenGLXHook_t* OpenGLXHook_t::_Instance = nullptr;

bool OpenGLXHook_t::StartHook(std::function<void()> keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount, /*ImFontAtlas* */ void* imguiFontAtlas)
{
    if (!_Hooked)
    {
        if (_GLXSwapBuffers == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook OpenGLX: Rendering functions missing.");
            return false;
        }

        if (!X11Hook_t::Inst()->StartHook(keyCombinationCallback, toggleKeys, toggleKeysCount))
            return false;

        _X11Hooked = true;

        BeginHook();
        TRY_HOOK_FUNCTION_OR_FAIL(GLXSwapBuffers);
        EndHook();

        INGAMEOVERLAY_INFO("Hooked OpenGLX");
        _Hooked = true;
        _ImGuiFontAtlas = imguiFontAtlas;
    }
    return true;
}

void OpenGLXHook_t::HideAppInputs(bool hide)
{
    if (_Initialized)
        X11Hook_t::Inst()->HideAppInputs(hide);
}

void OpenGLXHook_t::HideOverlayInputs(bool hide)
{
    if (_Initialized)
        X11Hook_t::Inst()->HideOverlayInputs(hide);
}

bool OpenGLXHook_t::IsStarted()
{
    return _Hooked;
}

void OpenGLXHook_t::_ResetRenderState(OverlayHookState state)
{
    if (_HookState == state)
        return;

    OverlayHookReady(state);

    _HookState = state;

    switch (state)
    {
        case OverlayHookState::Reset:
            break;

        case OverlayHookState::Removing:
            ImGui_ImplOpenGL3_Shutdown();
            X11Hook_t::Inst()->ResetRenderState(state);
            //ImGui::DestroyContext();

            _ImageResources.clear();

            //glXDestroyContext(_Display, _Context);
            _Display = nullptr;
            _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void OpenGLXHook_t::_PrepareForOverlay(Display* display, GLXDrawable drawable)
{
    if( !_Initialized )
    {
        if (ImGui::GetCurrentContext() == nullptr)
            ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));

        //int attributes[] = { //can't be const b/c X11 doesn't like it.  Not sure if that's intentional or just stupid.
        //    GLX_RGBA, //apparently nothing comes after this?
        //    GLX_RED_SIZE,    8,
        //    GLX_GREEN_SIZE,  8,
        //    GLX_BLUE_SIZE,   8,
        //    GLX_ALPHA_SIZE,  8,
        //    //Ideally, the size would be 32 (or at least 24), but I have actually seen
        //    //  this size (on a modern OS even).
        //    GLX_DEPTH_SIZE, 16,
        //    GLX_DOUBLEBUFFER, True,
        //    None
        //};
        //
        //XVisualInfo* visual_info = glXChooseVisual(_Display, DefaultScreen(_Display), attributes);
        //if (visual_info == nullptr)
        //    return;
        //
        //_Context = glXCreateContext(_Display, visual_info, nullptr, True);
        //if (_Context == nullptr)
        //    return;


        if (!X11Hook_t::Inst()->SetInitialWindowSize((Window)drawable))
            return;

        ImGui_ImplOpenGL3_Init();

        _Display = display;
        _Initialized = true;
        _ResetRenderState(OverlayHookState::Ready);
    }

    //auto oldContext = glXGetCurrentContext();

    //glXMakeCurrent(_Display, drawable, _Context);

    if (ImGui_ImplOpenGL3_NewFrame() && X11Hook_t::Inst()->PrepareForOverlay((Window)drawable))
    {
        auto screenshotType = _ScreenshotType();
        if (screenshotType == ScreenshotType_t::BeforeOverlay)
            _HandleScreenshot();

        ImGui::NewFrame();

        OverlayProc();

        _LoadResources();

        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (screenshotType == ScreenshotType_t::AfterOverlay)
            _HandleScreenshot();
    }

    //glXMakeCurrent(_Display, drawable, oldContext);
}

void OpenGLXHook_t::_HandleScreenshot()
{
    int viewport[8];
    int width, height;
    glGetIntegerv(GL_VIEWPORT, viewport); // viewport[2] = width, viewport[3] = height
    width = viewport[2];
    height = viewport[3];

    int bytesPerPixel = 4;

    std::vector<uint8_t> buffer(width * height * bytesPerPixel);

    GLboolean isDoubleBuffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &isDoubleBuffered);

    glReadBuffer(isDoubleBuffered ? GL_BACK : GL_FRONT);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

    std::vector<uint8_t> lineBuffer(width * bytesPerPixel);

    for (int i = 0; i < (height / 2); ++i)
    {
        uint8_t* topLine = buffer.data() + i * width * bytesPerPixel;
        uint8_t* bottomLine = buffer.data() + (height - i - 1) * width * bytesPerPixel;

        memcpy(lineBuffer.data(), topLine, width * bytesPerPixel);
        memcpy(topLine, bottomLine, width * bytesPerPixel);
        memcpy(bottomLine, lineBuffer.data(), width * bytesPerPixel);
    }

    ScreenshotCallbackParameter_t screenshot;
    screenshot.Width = width;
    screenshot.Height = height;
    screenshot.Pitch = bytesPerPixel * width;
    screenshot.Data = reinterpret_cast<void*>(buffer.data());
    screenshot.Format = InGameOverlay::ScreenshotDataFormat_t::R8G8B8A8;

    _SendScreenshot(&screenshot);
}

void OpenGLXHook_t::_MyGLXSwapBuffers(Display* display, GLXDrawable drawable)
{
    OpenGLXHook_t::Inst()->_PrepareForOverlay(display, drawable);
    OpenGLXHook_t::Inst()->_GLXSwapBuffers(display, drawable);
}

OpenGLXHook_t::OpenGLXHook_t():
    _Hooked(false),
    _X11Hooked(false),
    _Initialized(false),
    _HookState(OverlayHookState::Removing),
    _Display(nullptr),
    _ImGuiFontAtlas(nullptr),
    _GLXSwapBuffers(nullptr)
{
    //_library = dlopen(DLL_NAME);
}

OpenGLXHook_t::~OpenGLXHook_t()
{
    INGAMEOVERLAY_INFO("OpenGLX Hook removed");

    if (_X11Hooked)
        delete X11Hook_t::Inst();

    _ResetRenderState(OverlayHookState::Removing);

    //dlclose(_library);

    _Instance->UnhookAll();
    _Instance = nullptr;
}

OpenGLXHook_t* OpenGLXHook_t::Inst()
{
    if (_Instance == nullptr)
        _Instance = new OpenGLXHook_t;

    return _Instance;
}

const char* OpenGLXHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

RendererHookType_t OpenGLXHook_t::GetRendererHookType() const
{
    return RendererHookType_t::OpenGL;
}

void OpenGLXHook_t::LoadFunctions(decltype(::glXSwapBuffers)* pfnglXSwapBuffers)
{
    _GLXSwapBuffers = pfnglXSwapBuffers;
}

std::weak_ptr<uint64_t> OpenGLXHook_t::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    GLuint* texture = new GLuint(0);
    glGenTextures(1, texture);
    if (glGetError() != GL_NO_ERROR)
    {
        delete texture;
        return std::shared_ptr<uint64_t>(nullptr);
    }
    
    // Save old texture id
    GLint oldTex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);

    glBindTexture(GL_TEXTURE_2D, *texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    glBindTexture(GL_TEXTURE_2D, oldTex);

    auto ptr = std::shared_ptr<uint64_t>((uint64_t*)texture, [](uint64_t* handle)
    {
        if (handle != nullptr)
        {
            GLuint* texture = (GLuint*)handle;
            glDeleteTextures(1, texture);
            delete texture;
        }
    });

    _ImageResources.emplace(ptr);
    return ptr;
}

void OpenGLXHook_t::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}

}// namespace InGameOverlay