#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/../common/ingameoverlay_imconfig.h" -DINGAMEOVERLAY_BUILD_TESTS=ON -S ../../ -B ../../OUT/macos_metal &&\
cmake --build ../../OUT/macos_metal

#../../OUT/macos_metal/macos_metal_app
