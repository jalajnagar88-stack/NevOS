# NEVOS firmware for ESP32-S3.
#
#   . $HOME/esp/esp-idf/export.sh
#   idf.py -C targets/esp32s3 set-target esp32s3
#   idf.py -C targets/esp32s3 build flash monitor
#
# M1 scope: nev_port and nev_kernel running on real silicon. The display stack
# arrives with the panel driver in M5 — see components/nev_board/src/esp32s3.
cmake_minimum_required(VERSION 3.20)

#
# LVGL is named directly rather than by listing third_party/, because IDF treats
# a listed directory as a component when it has a CMakeLists.txt and otherwise
# scans its children — and third_party/unity has a root CMakeLists.txt of its
# own that never calls idf_component_register, so scanning would pull the host
# test framework into the firmware build and fail on it.
#
# LVGL 9 registers itself as an IDF component through env_support/cmake/esp.cmake
# when ESP_PLATFORM is set. Without this line the build stops at "Failed to
# resolve component 'lvgl'" before a single file is compiled, which is why no
# part of this firmware had ever been built for Xtensa.
#
set(EXTRA_COMPONENT_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/../../components
  ${CMAKE_CURRENT_LIST_DIR}/../../apps
  ${CMAKE_CURRENT_LIST_DIR}/../../third_party/lvgl
)

#
# One LVGL configuration for both targets.
#
# LVGL's ESP component compiles with -DLV_CONF_INCLUDE_SIMPLE, which makes it
# `#include "lv_conf.h"` from the include path. The host build points at
# config/lv_conf.h through LV_CONF_PATH; this puts the same file on the path
# here, so the device and the simulator are configured identically. Two
# different LVGL configurations would make "the same code runs on both" mean
# very little — the memory ceiling, the enabled widgets and the colour depth
# are all in that file.
#
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/../../config" APPEND)

set(NEVOS_BOARD "devkit_480" CACHE STRING "Board definition under boards/")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nevos)
