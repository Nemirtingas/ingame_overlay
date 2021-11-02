
cmake_policy(SET CMP0091 NEW)
project(ingame_overlay)
cmake_minimum_required(VERSION 3.0)

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

  enable_language(OBJCXX)
  link_libraries("-framework AppKit")

  set(INGAMEOVERLAY_SOURCES
    src/Base_Hook.cpp
    src/Renderer_Detector.cpp
    src/macosx/cppNSEvent.mm
    src/macosx/cppNSObject.mm
    src/macosx/cppNSView.mm
    src/macosx/objc_wrappers.mm
    src/macosx/NSView_Hook.cpp
    src/macosx/OpenGL_Hook.cpp
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

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests." OFF)

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

set(SYSTEM_CODE OFF CACHE BOOL "")
set(FILESYSTEM_CODE OFF CACHE BOOL "")
add_subdirectory(deps/System EXCLUDE_FROM_ALL)
add_subdirectory(deps/mini_detour EXCLUDE_FROM_ALL)

add_library(ingame_overlay
  ${INGAMEOVERLAY_SOURCES}
  ${IMGUI_SOURCES}
)

set_property(TARGET ingame_overlay PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET ingame_overlay PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(ingame_overlay
  PUBLIC
  
  System::System
  MiniDetour::MiniDetour
  
  $<$<BOOL:${WIN32}>:opengl32>
  $<$<BOOL:${UNIX}>:dl>
  $<$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>:GL>
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
  $<BUILD_INTERFACE:${IMGUI_USER_CONFIG_VALUE}>
  PUBLIC
  $<INSTALL_INTERFACE:${IMGUI_USER_CONFIG_NAME_VALUE}>
)

add_library(InGameOverlay::InGameOverlay ALIAS ingame_overlay)
set_target_properties(ingame_overlay PROPERTIES EXPORT_NAME InGameOverlay)

if(${BUILD_TESTS})

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

set_property(TARGET dx11_app PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(overlay_example SHARED
  tests/dx11/library_main.cpp
)

set_property(TARGET overlay_example PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET overlay_example PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(overlay_example
  PRIVATE
  InGameOverlay::InGameOverlay
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

