#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/../common/ingameoverlay_imconfig.h" -DINGAMEOVERLAY_BUILD_TESTS=ON -S ../../ -B ../../OUT/linux_vulkan &&\
cmake --build ../../OUT/linux_vulkan

#../../OUT/linux_vulkan/linux_vulkan_app