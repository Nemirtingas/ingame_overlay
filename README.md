# InGameOverlay

InGameOverlay is a cross-platform library for attaching custom overlays to third-party applications by hooking into their rendering pipeline. Rather than relying on access to the game or application source code, it is designed for injecting UI into processes where the renderer is already in place and the overlay needs to appear alongside the existing frame output.

## Highlights

- Cross-platform support for Windows, Linux, and macOS
- Renderer integration for OpenGL, Vulkan, DirectX 9/10/11/12, and Metal
- Reusable hook and resource infrastructure for overlay rendering
- Designed for injected overlays, tooling, and runtime visualization in external processes

## Linux and macOS overlays in action

These screenshots showcase the overlay running on Linux and macOS environments:

![Linux OpenGL overlay](public/Linux_OpenGL.png)

![Linux Vulkan overlay](public/Linux_Vulkan.png)

![macOS OpenGL2 overlay](public/MacOS_OpenGL2.png)

![macOS OpenGL3 overlay](public/MacOS_OpenGL3.png)

## Building

From the project root, configure and build with CMake:

```bash
cmake -S . -B build
cmake --build build
```

You can also enable the test targets with:

```bash
cmake -S . -B build -DINGAMEOVERLAY_BUILD_TESTS=ON
```

To enable library logs with spdlog, clone the dependency into the bundled deps folder and configure the project with the logging option enabled:

```bash
git clone https://www.github.com/gabime/spdlog deps/spdlog
cmake -S . -B build -DINGAMEOVERLAY_USE_SPDLOG=ON
```

## Project layout

- src/ contains the platform-specific hooks and internal overlay logic
- include/InGameOverlay/ contains the public headers
- tests/ contains example and validation projects for supported renderers

## License

This project is licensed under the GNU General Public License (GPL). See [LICENSE](LICENSE).
