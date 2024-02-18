#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/ingameoverlay_imconfig.h" -DINGAMEOVERLAY_BUILD_TESTS=ON -S ../../ -B ../../OUT/macos_opengl2 &&\
cmake --build ../../OUT/macos_opengl2

#../../OUT/macos_opengl2/macos_opengl2_app