#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/../common/ingameoverlay_imconfig.h" -DINGAMEOVERLAY_BUILD_TESTS=ON -S ../../ -B ../../OUT/macos_opengl3 &&\
cmake --build ../../OUT/macos_opengl3

#../../OUT/macos_opengl2/macos_opengl3_app