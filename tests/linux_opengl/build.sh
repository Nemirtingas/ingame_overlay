#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/ingameoverlay_imconfig.h" -DINGAMEOVERLAY_BUILD_TESTS=ON -S ../../ -B ../../OUT/linux_opengl &&\
cmake --build ../../OUT/linux_opengl

#../../OUT/linux_opengl/linux_opengl_app
