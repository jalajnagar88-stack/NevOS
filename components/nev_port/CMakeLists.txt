# ESP-IDF component manifest. The host build reads sources.cmake directly.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

idf_component_register(
  SRCS ${NEV_PORT_ESP_SRCS}
  INCLUDE_DIRS include
  REQUIRES freertos esp_timer esp_system heap
)
