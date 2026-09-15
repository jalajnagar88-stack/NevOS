# NEVOS simulator (host target).
#
#   cmake -S targets/host -B build/host -GNinja -DNEVOS_DISPLAY=sdl2
#   ninja -C build/host && ./build/host/nevos_sim
#
# NEVOS_DISPLAY=headless builds without SDL2 at all: no window, no display
# server, framebuffer captured to PPM. That is what CI runs.
cmake_minimum_required(VERSION 3.20)
project(nevos_sim C)

set(NEVOS_DISPLAY "sdl2" CACHE STRING "Simulator display backend: sdl2 or headless")
set_property(CACHE NEVOS_DISPLAY PROPERTY STRINGS sdl2 headless)
set(NEVOS_BOARD "devkit_480" CACHE STRING "Board definition under boards/")
option(NEVOS_WERROR "Treat warnings as errors" ON)

get_filename_component(NEVOS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
include(${NEVOS_ROOT}/cmake/nevos_host.cmake)
include(${NEVOS_ROOT}/components/nev_board/sources.cmake)
include(${NEVOS_ROOT}/components/nev_services/sources.cmake)

if(NOT EXISTS ${NEVOS_ROOT}/third_party/lvgl/lvgl.h)
  message(FATAL_ERROR "LVGL is missing. Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------- LVGL
set(LV_CONF_PATH ${NEVOS_ROOT}/config/lv_conf.h CACHE FILEPATH "" FORCE)
add_subdirectory(${NEVOS_ROOT}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl PUBLIC ${NEVOS_ROOT}/config)

# --------------------------------------------------------------------- board
add_library(nevos_board STATIC ${NEV_BOARD_SIM_SRCS})
target_include_directories(nevos_board PUBLIC
  ${NEV_BOARD_INCLUDE_DIRS}
  ${NEVOS_ROOT}/boards/${NEVOS_BOARD}
)
target_link_libraries(nevos_board PUBLIC nevos_core)

if(NEVOS_DISPLAY STREQUAL "sdl2")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  target_sources(nevos_board PRIVATE ${NEV_BOARD_SIM_SDL_SRCS})
  target_link_libraries(nevos_board PRIVATE PkgConfig::SDL2)
elseif(NEVOS_DISPLAY STREQUAL "headless")
  target_sources(nevos_board PRIVATE ${NEV_BOARD_SIM_HEADLESS_SRCS})
else()
  message(FATAL_ERROR "NEVOS_DISPLAY must be sdl2 or headless, got '${NEVOS_DISPLAY}'")
endif()

# ------------------------------------------------------------------ services
add_library(nevos_services STATIC ${NEV_SERVICES_SRCS})
target_include_directories(nevos_services PUBLIC ${NEV_SERVICES_INCLUDE_DIRS})
target_link_libraries(nevos_services PUBLIC nevos_core nevos_board lvgl)

# ----------------------------------------------------------------- simulator
add_executable(nevos_sim main.c)
target_link_libraries(nevos_sim PRIVATE nevos_services nevos_board nevos_core lvgl)
set_target_properties(nevos_sim PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

message(STATUS "NEVOS: board=${NEVOS_BOARD} display=${NEVOS_DISPLAY}")
