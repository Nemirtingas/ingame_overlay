# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project overview

InGameOverlay is a cross-platform C++17 library that injects a Dear ImGui overlay into third-party processes by hooking their rendering API (no access to the host application's source). It is built as a static library (`ingame_overlay`, CMake alias `Nemirtingas::InGameOverlay`) that a consumer links into an injected shared library.

Supported renderers per platform:

| Platform | Renderers | Window/input hook |
|----------|-----------|-------------------|
| Windows  | DirectX 9/10/11/12, OpenGL, Vulkan | `src/Windows/WindowsHook.*` (Win32 WndProc + Windows.Gaming.Input) |
| Linux    | OpenGL (GLX), Vulkan | `src/Linux/X11Hook.*` (X11 / xcb) |
| macOS    | OpenGL (2 & 3), Metal | `src/MacOSX/NSViewHook.*` (Objective-C++, `.mm`) |

## Repository layout

- `include/InGameOverlay/` — **public API**, the only headers consumers see:
  - `RendererDetector.h` — `DetectRenderer`, `GetDetectedRenderer`, `GetRenderer`, `StopRendererDetection`, `FreeDetector`.
  - `RendererHook.h` — abstract `RendererHook_t` (start hook, input policy, resources, screenshots) and enums (`RendererHookType_t` flags, `ToggleKey`, `OverlayHookState`, screenshot formats).
  - `RendererResource.h` — abstract `RendererResource_t` (GPU image resources usable with `ImGui::Image`).
- `src/` — implementation:
  - `BaseHook.*` — wraps `mini_detour` (`BeginHook`/`HookFunc`/`EndHook`/`UnhookAll`).
  - `RendererHookInternal.*`, `RendererResourceInternal.*` — shared base for all renderer hooks (resource batching, screenshots).
  - `Library.*` — cross-platform dynamic library helpers (`LoadLibraryFromPath`, `GetLibrarySymbol`, …).
  - `InternalIncludes.h` — logging macros `INGAMEOVERLAY_TRACE/DEBUG/INFO/WARN/ERROR` (no-ops unless `INGAMEOVERLAY_USE_SPDLOG`).
  - `OSDetector.h` — platform/arch macros.
  - `Windows/`, `Linux/`, `MacOSX/` — per-platform `RendererDetector` and `<Api>Hook` classes.
  - `glad2/`, `VulkanSDK/` — vendored GL loader and Vulkan headers. Do not edit.
- `deps/` — git submodules: `ImGui` (Nemirtingas fork, contains custom backends such as `imgui_win_shader_blobs`), `mini_detour`. `deps/spdlog` is optional and git-ignored (clone manually).
- `tests/` — manual test apps, not unit tests:
  - `dx9`, `dx10`, `dx11`, `dx12`, `windows_opengl3`, `windows_vulkan`, `linux_opengl`, `linux_vulkan`, `macos_opengl2`, `macos_opengl3`, `macos_metal` — standalone renderer host apps.
  - `overlay_example`, `test_overlay` — shared libraries using the public API; inject/preload them into a host app to exercise the hooks.
  - `windows_shaders_precompile` — generator for the precompiled DirectX shader blobs (`INGAMEOVERLAY_BUILD_WINDOWS_SHADERS=ON`).
  - `common/` — shared helpers, `ingameoverlay_imconfig.h`, `glfw` submodule, prebuilt Vulkan loader libs.
- `OUT/` — local build output directories (git-ignored).

## Build

Submodules are required:

```bash
git submodule update --init --recursive
```

Configure and build (this mirrors CI in `.github/workflows/main.yml`):

```bash
cmake -S . -B build -DINGAMEOVERLAY_BUILD_TESTS=ON -DIMGUI_USER_CONFIG=<repo>/tests/common/ingameoverlay_imconfig.h
cmake --build build
```

- Windows: add `-A x64` or `-A Win32`.
- Linux needs `libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcb1-dev libxcb-keysyms1-dev libxcb-xinput-dev libxcb-xkb-dev`.
- Vulkan test apps are only built for 64-bit targets.
- `IMGUI_USER_CONFIG` must define `ImTextureID` as `uint64_t` (see `tests/common/ingameoverlay_imconfig.h`); `RendererResource_t::GetResourceId()` returns `uint64_t`.

CMake options:

| Option | Default | Effect |
|--------|---------|--------|
| `INGAMEOVERLAY_BUILD_TESTS` | OFF | Build the test apps and example overlays |
| `INGAMEOVERLAY_USE_SPDLOG` | OFF | Enable logging (requires `deps/spdlog`) |
| `INGAMEOVERLAY_DYNAMIC_RUNTIME` | ON | MSVC `/MD` vs `/MT` |
| `INGAMEOVERLAY_USE_SYSTEM_LIBRARIES` | OFF | `find_package` spdlog/MiniDetour instead of `deps/` |
| `INGAMEOVERLAY_BUILD_WINDOWS_SHADERS` | OFF | Build the shader precompiler (Windows only) |

There is no automated test suite. "Testing" means the CI build succeeds on Windows (x86/x64), Linux (x86/x64) and macOS (x64), plus manual runs of a host app with an overlay injected. When you cannot run a platform locally, say so instead of claiming it works.

## Architecture notes

- **Detection** (`src/<OS>/RendererDetector.*`): hooks the presentation entry points of every candidate API (creating dummy windows/devices to get vtables), waits for the application to present a frame, then returns the matching `RendererHook_t`. DXVK is detected and reported as the DirectX type, not Vulkan.
- **Renderer hooks** (`<Api>Hook_t`): inherit `RendererHookInternal_t` + `BaseHook_t`, are singletons (`static _Instance`, `Inst()`), store original function pointers as `decltype(&Iface::Method) _IfaceMethod;` and redirect them to `_MyIfaceMethod` via the `TRY_HOOK_FUNCTION` / `TRY_HOOK_FUNCTION_OR_FAIL` macros.
- **Lifecycle**: `StartHook` installs hooks and the window hook; the first present initialises ImGui backends and fires `OverlayHookReady(OverlayHookState::Ready)`; resize/device loss fires `Reset`; teardown fires `Removing`. `OverlayProc` is called every frame inside the ImGui frame.
- **Resources**: textures are loaded on the render thread, in batches (`SetAutoLoadBatchSize`, default 10) and released with a frame delay (`RendererTextureReleaseParameter_t::ReleaseFrame`) so in-flight frames are not affected. `RendererResource_t` does not own attached pixel data.
- **Device ref counts**: hooks track how many references they themselves hold on the app's device (e.g. `DX11Hook_t::_UpdateHookDeviceRefCount`) to detect when the application really releases it. Any change that adds/removes a `Get*`/`AddRef`/`Release` on the device must update this accounting.
- Symbol visibility is hidden by default; the library is meant to be linked into an injected module and must not pollute the host process.

## Code conventions

- C++17, 4-space indentation, Allman braces, `#pragma once`.
- Every source file starts with the existing LGPLv3 copyright header — copy it for new files.
- Naming:
  - Types end with `_t` (`RendererHook_t`), internal enums may end with `_e`.
  - Private/protected members and methods: leading underscore + PascalCase (`_DeviceContext`, `_PrepareForOverlay`).
  - Public methods: PascalCase. Locals and parameters: camelCase.
  - Hook trampolines: `_IfaceMethod` (original) / `_MyIfaceMethod` (replacement).
- Everything lives in `namespace InGameOverlay`.
- Log through the `INGAMEOVERLAY_*` macros only (fmt-style `{}` placeholders); never `printf`/`std::cout` in library code.
- Hooked functions run on the application's render thread inside a foreign process: avoid exceptions, blocking calls and allocations in hot paths, and always restore any pipeline state you change.
- New source files must be added to the matching platform list in `CMakeLists.txt` (`INGAMEOVERLAY_SOURCES` / `PRIVATE_INGAMEOVERLAY_HEADERS`); globs are not used.
- Keep public headers free of platform/graphics API includes; they must compile for consumers on every platform.
- Changing anything in `include/InGameOverlay/` is an ABI/API change for downstream emulators — keep it minimal and backward compatible where possible.

## Do not

- Edit vendored code (`src/glad2`, `src/VulkanSDK`, `tests/common/vulkansdk`, `tests/common/stb_image.h`) or submodule contents in `deps/` from this repository; changes to ImGui or mini_detour belong in their own repos.
- Commit build output (`OUT/`, `build/`) or `deps/spdlog`.
