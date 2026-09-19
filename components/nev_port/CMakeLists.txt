# ESP-IDF component manifest. The host build reads sources.cmake directly.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

idf_component_register(
  SRCS ${NEV_PORT_ESP_SRCS}
  INCLUDE_DIRS include
  # lwip: nev_net_posix.c is shared with the host and uses the BSD socket API,
  # which on ESP-IDF is lwIP under the standard headers.
  REQUIRES freertos esp_timer esp_system heap lwip
)
