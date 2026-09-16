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
include(${NEVOS_ROOT}/components/nev_appkit/sources.cmake)
include(${NEVOS_ROOT}/apps/game_engine/sources.cmake)
include(${NEVOS_ROOT}/apps/sources.cmake)

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

# ------------------------------------------------------------------- persona
# The renderers only; the parameter model is already in nevos_core.
add_library(nevos_persona STATIC
  ${NEV_PERSONA_DIR}/src/face_registry.c
  ${NEV_PERSONA_DIR}/src/face_vector.c
  ${NEV_PERSONA_DIR}/src/persona.c
)
target_include_directories(nevos_persona PUBLIC ${NEV_PERSONA_INCLUDE_DIRS})
target_link_libraries(nevos_persona PUBLIC nevos_core lvgl)

# ------------------------------------------------------------------- appkit
add_library(nevos_appkit STATIC ${NEV_APPKIT_SRCS})
target_include_directories(nevos_appkit PUBLIC ${NEV_APPKIT_INCLUDE_DIRS})
target_link_libraries(nevos_appkit PUBLIC nevos_core lvgl)

# ---------------------------------------------------------------------- apps
# Apps self-register from constructors, so the linker must not discard them for
# having no referenced symbol. Whole-archive is what keeps "adding an app is
# adding a folder" true.
add_library(nevos_game STATIC ${NEV_GAME_SRCS})
target_include_directories(nevos_game PUBLIC ${NEV_GAME_INCLUDE_DIRS})
target_link_libraries(nevos_game PUBLIC nevos_appkit)

add_library(nevos_apps STATIC ${NEV_APP_SRCS})
target_include_directories(nevos_apps PRIVATE ${NEV_GAME_RULE_INCLUDE_DIRS})
target_link_libraries(nevos_apps PUBLIC nevos_appkit nevos_game)

# ----------------------------------------------------------------- simulator
add_executable(nevos_sim main.c)
target_link_libraries(nevos_sim PRIVATE
  nevos_services nevos_persona nevos_appkit nevos_game nevos_board nevos_core lvgl)

# Force-link the apps archive so self-registering translation units survive: an
# app's only symbol is its constructor, and nothing references it.
#
# LINK_LIBRARY:WHOLE_ARCHIVE rather than a raw -Wl flag. The raw flag does not
# take part in CMake's dependency ordering, so the archive landed after the
# libraries it needs and the engine's symbols came up undefined. This also
# spells itself correctly on both GNU ld and Apple's -force_load.
target_link_libraries(nevos_sim PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nevos_apps>")
set_target_properties(nevos_sim PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

message(STATUS "NEVOS: board=${NEVOS_BOARD} display=${NEVOS_DISPLAY}")
