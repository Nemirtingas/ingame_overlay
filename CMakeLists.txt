cmake_policy(SET CMP0091 NEW)
project(ingame_overlay)
cmake_minimum_required(VERSION 3.15)

if(WIN32) # Setup some variables for Windows build
  set(INGAMEOVERLAY_SOURCES
    src/Base_Hook.cpp
    src/Renderer_Detector.cpp
    src/windows/DX9_Hook.cpp
    src/windows/DX10_Hook.cpp
    src/windows/DX11_Hook.cpp
    src/windows/DX12_Hook.cpp
    src/windows/OpenGL_Hook.cpp
    src/windows/Vulkan_Hook.cpp
    src/windows/Windows_Hook.cpp
  )

  set(PRIVATE_INGAMEOVERLAY_HEADERS
    src/Base_Hook.h
    src/windows/DX9_Hook.h
    src/windows/DX10_Hook.h
    src/windows/DX11_Hook.h
    src/windows/DX12_Hook.h
    src/windows/OpenGL_Hook.h
    src/windows/Vulkan_Hook.h
    src/windows/Windows_Hook.h
  )

  set(IMGUI_SOURCES
    deps/ImGui/imgui.cpp
    deps/ImGui/imgui_draw.cpp
    deps/ImGui/imgui_tables.cpp
    deps/ImGui/imgui_widgets.cpp
    deps/ImGui/backends/imgui_impl_dx9.cpp
    deps/ImGui/backends/imgui_impl_dx10.cpp
    deps/ImGui/backends/imgui_impl_dx11.cpp
    deps/ImGui/backends/imgui_impl_dx12.cpp
    deps/ImGui/backends/imgui_impl_opengl3.cpp
    deps/ImGui/backends/imgui_impl_vulkan.cpp
    deps/ImGui/backends/imgui_impl_win32.cpp
    deps/ImGui/backends/imgui_win_shader_blobs.cpp
  )

elseif(APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -stdlib=libc++")

  enable_language(OBJCXX)
  link_libraries("-framework AppKit -framework GameController -framework Carbon")

  set(INGAMEOVERLAY_SOURCES
    src/Base_Hook.cpp
    src/Renderer_Detector.cpp
    src/macosx/NSView_Hook.mm
    src/macosx/OpenGL_Hook.cpp
  )

  set(PRIVATE_INGAMEOVERLAY_HEADERS
    src/Base_Hook.h
    src/macosx/NSView_Hook.h
    src/macosx/OpenGL_Hook.h
  )

  set(IMGUI_SOURCES
    deps/ImGui/imgui.cpp
    deps/ImGui/imgui_draw.cpp
    deps/ImGui/imgui_tables.cpp
    deps/ImGui/imgui_widgets.cpp
    deps/ImGui/backends/imgui_impl_opengl2.cpp
    deps/ImGui/backends/imgui_impl_osx.mm
  )

elseif(UNIX)

  set(INGAMEOVERLAY_SOURCES
    src/Base_Hook.cpp
    src/Renderer_Detector.cpp
    src/linux/OpenGLX_Hook.cpp
    src/linux/X11_Hook.cpp
  )

  set(PRIVATE_INGAMEOVERLAY_HEADERS
    src/Base_Hook.h
        src/linux/OpenGLX_Hook.h
    src/linux/X11_Hook.h
  )

  set(IMGUI_SOURCES
    deps/ImGui/imgui.cpp
    deps/ImGui/imgui_draw.cpp
    deps/ImGui/imgui_tables.cpp
    deps/ImGui/imgui_widgets.cpp
    deps/ImGui/backends/imgui_impl_vulkan.cpp
    deps/ImGui/backends/imgui_impl_opengl3.cpp
    deps/ImGui/backends/imgui_impl_x11.cpp
  )

else()
  message(FATAL_ERROR "No CMake for other platforms")

endif()

set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_INGAMEOVERLAY_TESTS "Build tests." OFF)
option(USE_SPDLOG "Enable logs with SPDLOG." OFF)

find_package(Threads REQUIRED)

if(NOT ${IMGUI_USER_CONFIG} STREQUAL "")
  set(IMGUI_USER_CONFIG_VALUE IMGUI_USER_CONFIG="${IMGUI_USER_CONFIG}")
  get_filename_component(IMGUI_USER_CONFIG_NAME_VALUE ${IMGUI_USER_CONFIG} NAME)
  set(IMGUI_USER_CONFIG_NAME_VALUE IMGUI_USER_CONFIG="${IMGUI_USER_CONFIG_NAME_VALUE}")
else()
  set(IMGUI_USER_CONFIG "" CACHE FILEPATH "")
endif()

set(INGAMEOVERLAY_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ingame_overlay/Renderer_Hook.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ingame_overlay/Renderer_Detector.h
)

set(INGAMEOVERLAY_IMGUI_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imconfig.h
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imgui.h
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imgui_internal.h
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imstb_rectpack.h
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imstb_textedit.h
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/imstb_truetype.h
  $<$<BOOL:${IMGUI_USER_CONFIG}>:${IMGUI_USER_CONFIG}>
)

add_subdirectory(deps/System EXCLUDE_FROM_ALL)
add_subdirectory(deps/mini_detour EXCLUDE_FROM_ALL)

add_library(ingame_overlay
  ${INGAMEOVERLAY_SOURCES}
  ${IMGUI_SOURCES}
  ${INGAMEOVERLAY_HEADERS}
  ${PRIVATE_INGAMEOVERLAY_HEADERS}
)

set_target_properties(ingame_overlay PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

target_link_libraries(ingame_overlay
  PUBLIC

  System::System
  MiniDetour::MiniDetour

  $<$<BOOL:${USE_SPDLOG}>:spdlog::spdlog>

  $<$<BOOL:${WIN32}>:opengl32>
  $<$<BOOL:${UNIX}>:dl>
  $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>:GL>
  $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>:X11>
)

target_compile_options(ingame_overlay
  PRIVATE
  $<$<BOOL:${MSVC}>:/MP>
)

target_include_directories(ingame_overlay
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan_sdk/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/glad2/include>

  $<INSTALL_INTERFACE:include>
)

target_compile_definitions(ingame_overlay
  PRIVATE
  $<$<BOOL:${USE_SPDLOG}>:USE_SPDLOG>
  $<BUILD_INTERFACE:${IMGUI_USER_CONFIG_VALUE}>
  $<BUILD_INTERFACE:IMGUI_DISABLE_DEMO_WINDOWS>
  PUBLIC
  $<INSTALL_INTERFACE:${IMGUI_USER_CONFIG_NAME_VALUE}>
  IMGUI_IMPL_OPENGL_LOADER_CUSTOM
  IMGUI_IMPL_OPENGL_LOADER_GLAD2
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  IMGUI_DISABLE_OBSOLETE_KEYIO
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

add_library(InGameOverlay::InGameOverlay ALIAS ingame_overlay)
set_target_properties(ingame_overlay PROPERTIES EXPORT_NAME InGameOverlay)

if(${BUILD_INGAMEOVERLAY_TESTS})

  if(WIN32)
    add_executable(dx9_app
      tests/dx9/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_dx9.cpp
      deps/ImGui/backends/imgui_impl_win32.cpp
      deps/ImGui/backends/imgui_win_shader_blobs.cpp
    )

    target_include_directories(dx9_app
      PRIVATE
      deps/ImGui
    )

    target_link_libraries(dx9_app
      PRIVATE
      user32
      d3d9
    )

    target_compile_definitions(dx9_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

    set_property(TARGET dx9_app PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(dx10_app
      tests/dx10/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_dx10.cpp
      deps/ImGui/backends/imgui_impl_win32.cpp
      deps/ImGui/backends/imgui_win_shader_blobs.cpp
    )

    target_include_directories(dx10_app
      PRIVATE
      deps/ImGui
    )

    target_link_libraries(dx10_app
      PRIVATE
      user32
      d3d10
    )

    target_compile_definitions(dx10_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
	  IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

    set_property(TARGET dx10_app PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(dx11_app
      tests/dx11/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_dx11.cpp
      deps/ImGui/backends/imgui_impl_win32.cpp
      deps/ImGui/backends/imgui_win_shader_blobs.cpp
    )

    target_include_directories(dx11_app
      PRIVATE
      deps/ImGui
    )

    target_link_libraries(dx11_app
      PRIVATE
      user32
      d3d11
    )

    target_compile_definitions(dx11_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

    set_property(TARGET dx11_app PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(dx12_app
      tests/dx12/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_dx12.cpp
      deps/ImGui/backends/imgui_impl_win32.cpp
      deps/ImGui/backends/imgui_win_shader_blobs.cpp
    )

    target_include_directories(dx12_app
      PRIVATE
      deps/ImGui
    )

    target_link_libraries(dx12_app
      PRIVATE
      user32
      d3d12
      dxgi
    )

    target_compile_definitions(dx12_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

    set_property(TARGET dx12_app PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(windows_opengl3_app
      tests/windows_opengl3/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_opengl3.cpp
      deps/ImGui/backends/imgui_impl_win32.cpp
    )

    target_include_directories(windows_opengl3_app
      PRIVATE
      deps/ImGui
	  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/glad2/include>
    )

    target_link_libraries(windows_opengl3_app
      PRIVATE
      user32
      opengl32
    )

    target_compile_definitions(windows_opengl3_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

    set_property(TARGET windows_opengl3_app PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

  elseif(APPLE)

	add_executable(macos_opengl2_app
      tests/macos_opengl2/main.mm
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_opengl2.cpp
      deps/ImGui/backends/imgui_impl_osx.mm
    )

    target_include_directories(macos_opengl2_app
      PRIVATE
      deps/ImGui
      ${CMAKE_CURRENT_SOURCE_DIR}/src/glad2/include
    )

    target_link_libraries(macos_opengl2_app
      PRIVATE
      dl
    )

    target_compile_definitions(macos_opengl2_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

  elseif(UNIX)

    add_executable(linux_opengl_app
      tests/linux_opengl/main.cpp
      deps/ImGui/imgui.cpp
      deps/ImGui/imgui_demo.cpp
      deps/ImGui/imgui_draw.cpp
      deps/ImGui/imgui_tables.cpp
      deps/ImGui/imgui_widgets.cpp
      deps/ImGui/backends/imgui_impl_opengl3.cpp
      deps/ImGui/backends/imgui_impl_x11.cpp
    )

    target_include_directories(linux_opengl_app
      PRIVATE
      deps/ImGui
      ${CMAKE_CURRENT_SOURCE_DIR}/src/glad2/include
    )

    target_link_libraries(linux_opengl_app
      PRIVATE
      dl
      GL
      X11
    )

    target_compile_definitions(linux_opengl_app
      PRIVATE
      ${IMGUI_USER_CONFIG_VALUE}
      IMGUI_IMPL_OPENGL_LOADER_CUSTOM
      IMGUI_IMPL_OPENGL_LOADER_GLAD2
      IMGUI_DISABLE_OBSOLETE_KEYIO
      IMGUI_DISABLE_OBSOLETE_FUNCTIONS
      IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    )

  endif()

  add_library(overlay_example SHARED
    tests/overlay_example/library_main.cpp
  )

  set_target_properties(overlay_example PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
  )

  target_link_options(overlay_example
    PRIVATE
    $<$<AND:$<NOT:$<BOOL:${APPLE}>>,$<BOOL:${UNIX}>>:-Wl,--exclude-libs,ALL>
    $<$<AND:$<NOT:$<BOOL:${APPLE}>>,$<BOOL:${UNIX}>>:-Wl,--no-undefined>
  )

  target_link_libraries(overlay_example
    PRIVATE
    InGameOverlay::InGameOverlay
    Threads::Threads
  )

endif()

##################
## Install rules
install(TARGETS ingame_overlay EXPORT InGameOverlayTargets
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(FILES ${INGAMEOVERLAY_HEADERS}
  DESTINATION include/ingame_overlay
)

install(FILES ${INGAMEOVERLAY_IMGUI_HEADERS}
  DESTINATION include/ingame_overlay/ImGui
)

# Export targets
install(
  EXPORT InGameOverlayTargets
  FILE InGameOverlayConfig.cmake
  NAMESPACE InGameOverlay::
  DESTINATION lib/cmake/InGameOverlay
)