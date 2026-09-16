# Single source of truth for nev_port's file list, read by both build systems
# (targets/esp32s3 via CMakeLists.txt, targets/host and targets/tests directly).
# Explicit rather than globbed so that adding a file is a visible change.

set(NEV_PORT_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_PORT_COMMON_SRCS
  ${NEV_PORT_DIR}/src/common/nev_err.c
  ${NEV_PORT_DIR}/src/common/nev_log.c
  ${NEV_PORT_DIR}/src/common/nev_wallclock.c
  # One implementation for both targets: ESP-IDF's lwIP provides the BSD socket
  # API under the standard headers. See the note at the top of the file.
  ${NEV_PORT_DIR}/src/common/nev_net_posix.c
  ${NEV_PORT_DIR}/src/common/nev_rand_common.c
)

set(NEV_PORT_HOST_SRCS
  ${NEV_PORT_COMMON_SRCS}
  ${NEV_PORT_DIR}/src/host/nev_time_host.c
  ${NEV_PORT_DIR}/src/host/nev_sync_host.c
  ${NEV_PORT_DIR}/src/host/nev_mem_host.c
  ${NEV_PORT_DIR}/src/host/nev_task_host.c
  ${NEV_PORT_DIR}/src/host/nev_panic_host.c
  ${NEV_PORT_DIR}/src/host/nev_rand_host.c
)

set(NEV_PORT_ESP_SRCS
  ${NEV_PORT_COMMON_SRCS}
  ${NEV_PORT_DIR}/src/esp32s3/nev_time_esp.c
  ${NEV_PORT_DIR}/src/esp32s3/nev_sync_esp.c
  ${NEV_PORT_DIR}/src/esp32s3/nev_mem_esp.c
  ${NEV_PORT_DIR}/src/esp32s3/nev_task_esp.c
  ${NEV_PORT_DIR}/src/esp32s3/nev_panic_esp.c
  ${NEV_PORT_DIR}/src/esp32s3/nev_rand_esp.c
)

set(NEV_PORT_INCLUDE_DIRS ${NEV_PORT_DIR}/include)
