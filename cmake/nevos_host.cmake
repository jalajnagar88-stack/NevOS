# Shared host-build definitions for the simulator and the test suite.
#
# Defines:
#   nevos_core   — L-1 (nev_port, host impl) + L1 (nev_kernel). No display, no
#                  SDL2, no RTOS. This is what unit tests link against.
#   nevos_warnings — the warning contract, applied to every NEVOS target.

get_filename_component(NEVOS_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

include(${NEVOS_ROOT}/components/nev_port/sources.cmake)
include(${NEVOS_ROOT}/components/nev_kernel/sources.cmake)
include(${NEVOS_ROOT}/components/nev_persona/sources.cmake)
include(${NEVOS_ROOT}/components/nev_bridge/sources.cmake)

add_library(nevos_warnings INTERFACE)
target_compile_options(nevos_warnings INTERFACE
  -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual
  -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter
  $<$<BOOL:${NEVOS_WERROR}>:-Werror>
)

# The face's parameter model is pure data and has no LVGL dependency, so it
# belongs in the toolkit-free core that unit tests link against. The renderers
# that draw it do not.
add_library(nevos_core STATIC
  ${NEV_PORT_HOST_SRCS}
  ${NEV_KERNEL_SRCS}
  ${NEV_KERNEL_HOST_SRCS}
  ${NEV_PERSONA_DIR}/src/face_presets.c
  ${NEV_PERSONA_DIR}/src/persona_core.c
  ${NEV_BRIDGE_CODEC_SRCS}
  ${NEV_BRIDGE_TRANSPORT_SRCS}
)
target_include_directories(nevos_core PUBLIC
  ${NEV_PORT_INCLUDE_DIRS}
  ${NEV_KERNEL_INCLUDE_DIRS}
  ${NEV_PERSONA_INCLUDE_DIRS}
  ${NEV_BRIDGE_INCLUDE_DIRS}
)
# The one thing the socket layer needs to know about its target: the host link
# is always up, where the device's depends on Wi-Fi.
target_compile_definitions(nevos_core PUBLIC NEV_TARGET_HOST)
target_link_libraries(nevos_core PUBLIC m)
set_target_properties(nevos_core PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(nevos_core PUBLIC nevos_warnings)

find_package(Threads REQUIRED)
target_link_libraries(nevos_core PUBLIC Threads::Threads)
