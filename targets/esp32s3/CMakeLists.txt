# NEVOS firmware for ESP32-S3.
#
#   . $HOME/esp/esp-idf/export.sh
#   idf.py -C targets/esp32s3 set-target esp32s3
#   idf.py -C targets/esp32s3 build flash monitor
#
# M1 scope: nev_port and nev_kernel running on real silicon. The display stack
# arrives with the panel driver in M5 — see components/nev_board/src/esp32s3.
cmake_minimum_required(VERSION 3.20)

set(EXTRA_COMPONENT_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/../../components
)

set(NEVOS_BOARD "devkit_480" CACHE STRING "Board definition under boards/")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nevos)
