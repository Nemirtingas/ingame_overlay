/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 */

#include "../../src/RendererHookInternal.h"

#include <cassert>

using namespace InGameOverlay;

class ScreenshotRequestTestHook_t : public RendererHookInternal_t
{
public:
    uint64_t RequestId() { return _ScreenshotRequestId(); }
    void Complete(uint64_t id, ScreenshotCallbackParameter_t* data) { _SendScreenshotForRequest(id, data); }
    ScreenshotType_t Type() { return _ScreenshotType(); }

    bool StartHook(std::function<void()>, ToggleKey[], int, void*) override { return false; }
    void HideAppInputs(bool) override {}
    void HideOverlayInputs(bool) override {}
    bool IsStarted() override { return false; }
    const char* GetLibraryName() const override { return "test"; }
    RendererHookType_t GetRendererHookType() const override { return RendererHookType_t::DirectX12; }
    std::weak_ptr<RendererTexture_t> AllocImageResource() override { return {}; }
    void LoadImageResource(RendererTextureLoadParameter_t&) override {}
    void ReleaseImageResource(std::weak_ptr<RendererTexture_t>) override {}
    void SetLibraryPath(std::filesystem::path const&) override {}
};

struct CallbackState_t
{
    ScreenshotRequestTestHook_t* Hook = nullptr;
    int Calls = 0;
    bool RequestAgain = false;
};

static void ScreenshotCallback(const ScreenshotCallbackParameter_t*, void* userParameter)
{
    auto* state = static_cast<CallbackState_t*>(userParameter);
    ++state->Calls;
    if (state->RequestAgain)
        state->Hook->TakeScreenshot(ScreenshotType_t::AfterOverlay);
}

int main()
{
    ScreenshotRequestTestHook_t hook;
    CallbackState_t callback{ &hook };
    hook.SetScreenshotCallback(ScreenshotCallback, &callback);

    hook.TakeScreenshot(ScreenshotType_t::BeforeOverlay);
    const uint64_t first = hook.RequestId();
    hook.TakeScreenshot(ScreenshotType_t::BeforeOverlay);
    const uint64_t sameTypeLater = hook.RequestId();
    hook.Complete(first, nullptr); // failed submission must not consume the newer request
    assert(hook.Type() == ScreenshotType_t::BeforeOverlay);
    hook.Complete(sameTypeLater, nullptr);
    assert(hook.Type() == ScreenshotType_t::None);

    hook.TakeScreenshot(ScreenshotType_t::BeforeOverlay);
    const uint64_t differentTypeCompletion = hook.RequestId();
    hook.TakeScreenshot(ScreenshotType_t::AfterOverlay);
    hook.Complete(differentTypeCompletion, nullptr);
    assert(hook.Type() == ScreenshotType_t::AfterOverlay);

    callback.RequestAgain = true;
    ScreenshotCallbackParameter_t data{};
    data.Format = ScreenshotDataFormat_t::R8G8B8A8;
    const uint64_t callbackCompletion = hook.RequestId();
    hook.Complete(callbackCompletion, &data);
    assert(callback.Calls == 1);
    assert(hook.Type() == ScreenshotType_t::AfterOverlay);

    // A new hook instance models teardown/reinitialization: completion from an old
    // generation cannot clear its request, even when the screenshot type matches.
    ScreenshotRequestTestHook_t reinitialized;
    reinitialized.TakeScreenshot(ScreenshotType_t::BeforeOverlay);
    reinitialized.Complete(callbackCompletion, nullptr);
    assert(reinitialized.Type() == ScreenshotType_t::BeforeOverlay);
}
