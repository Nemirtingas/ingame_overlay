#!/bin/bash

cd "$(dirname "$0")"

cmake -DIMGUI_USER_CONFIG="$(pwd)/ingameoverlay_imconfig.h" -DBUILD_INGAMEOVERLAY_TESTS=ON -S ../../ -B ../../OUT/macos_metal &&\
cmake --build ../../OUT/macos_metal

#../../OUT/macos_metal/macos_metal_app
