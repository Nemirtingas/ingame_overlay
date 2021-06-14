#CMAKE_TOOLCHAIN_FILE

project(ingame_overlay)
cmake_minimum_required(VERSION 3.0)

if(WIN32) # Setup some variables for Windows build
  if(MSVC) # If building with MSVC
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE) # Force to only build Debug & Release projects

    set(CompilerFlags
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        )
    foreach(CompilerFlag ${CompilerFlags})
      string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
    endforeach()

  endif()

  file(
    GLOB
    DearImGui_sources
    extra/ImGui/*.cpp
	extra/ImGui/backends/imgui_impl_dx9.cpp
	extra/ImGui/backends/imgui_impl_dx10.cpp
	extra/ImGui/backends/imgui_impl_dx11.cpp
	extra/ImGui/backends/imgui_impl_dx12.cpp
	extra/ImGui/backends/imgui_impl_opengl3.cpp
	extra/ImGui/backends/imgui_impl_vulkan.cpp
	extra/ImGui/backends/imgui_impl_win32.cpp
  )
  
  file(
    GLOB
    pfhooks_sources
	windows/*.cpp
  )

elseif(APPLE)
  message(STATUS "Apple build should work but the overlay is not detected/supported.")
  
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

  file(
    GLOB
    DearImGui_sources
    extra/ImGui/*.cpp
    extra/ImGui/backends/imgui_impl_opengl2.cpp
  )

  file(
    GLOB
    pfhooks_sources
	macosx/*.cpp
  )

elseif(UNIX)
  
  file(
    GLOB
    DearImGui_sources
    extra/ImGui/*.cpp
	extra/ImGui/backends/imgui_impl_opengl3.cpp
	extra/ImGui/backends/imgui_impl_vulkan.cpp
	extra/ImGui/backends/imgui_impl_x11.cpp
  )
  
  file(
    GLOB
    pfhooks_sources
	linux/*.cpp
  )
  
else()
  message(FATAL_ERROR "No CMake for other platforms")

endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(
  GLOB
  minidetour_sources
  extra/mini_detour/src/*.cpp
)

file(
  GLOB
  library_sources
  extra/library/src/*.cpp
)

########################################
## ingame_overlay library

add_library(
  ingame_overlay
  SHARED
  library_main.cpp
  image.cpp
  Base_Hook.cpp
  Renderer_Detector.cpp
  utils.cpp
  
  ${library_sources}
  ${pfhooks_sources}
  ${minidetour_sources}
  ${DearImGui_sources}
)

target_include_directories(
  ingame_overlay
  PRIVATE
  extra/stb
  extra/ImGui
  extra/mini_detour/include
  extra/library/include
  extra/glad2/include
  extra/vulkan_sdk/include
)

target_compile_definitions(
  ingame_overlay
  PRIVATE
  __EXPORT_SYMBOLS__
  IMGUI_USER_CONFIG="${CMAKE_SOURCE_DIR}/ingameoverlay_imconfig.h"
)

if(WIN32)
#################
## DX11 test
file(
  GLOB
  dx11_imgui_sources
  extra/ImGui/*.cpp
  extra/ImGui/backends/imgui_impl_dx11.cpp
  extra/ImGui/backends/imgui_impl_win32.cpp
)

add_executable(
  dx11_test
  tests/dx11/main.cpp
  ${dx11_imgui_sources}
)

target_link_libraries(
  dx11_test
  d3d11
)

target_include_directories(
  dx11_test
  PRIVATE
  extra/ImGui
)

add_dependencies(
  dx11_test
  ingame_overlay
)
endif()

##################
## Install rules
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR})


if(WIN32)
  if(X64)
    set(OUT_DIR win64)
  else()
    set(OUT_DIR win32)
  endif()
elseif(APPLE)
  if(X64)
    set(OUT_DIR macosx64)
  else()
    set(OUT_DIR macosx32)
  endif()
elseif(UNIX)
  if(X64)
    set(OUT_DIR linux64)
  else()
    set(OUT_DIR linux32)
  endif()
endif()

install(
  TARGETS ingame_overlay
  ARCHIVE DESTINATION release/${OUT_DIR}
  LIBRARY DESTINATION release/${OUT_DIR}
  RUNTIME DESTINATION release/${OUT_DIR}
#  PUBLIC_HEADER DESTINATION release/include/
)

INSTALL (
  DIRECTORY ${CMAKE_SOURCE_DIR}/include/
  DESTINATION release/include
  FILES_MATCHING PATTERN "*.h*"
                 PATTERN "*.inl*"
)
