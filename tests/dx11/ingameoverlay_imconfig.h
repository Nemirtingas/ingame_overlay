#pragma once

#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS         // [Win32] Don't implement default IME handler. Won't use and link with ImmGetContext/ImmSetCompositionWindow.

#include <stdint.h>
// ImTextureID [configurable type: override in imconfig.h with '#define ImTextureID xxx']
#define ImTextureID uint64_t

#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
