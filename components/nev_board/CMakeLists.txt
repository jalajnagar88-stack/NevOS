include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

if(NOT DEFINED NEVOS_BOARD)
  set(NEVOS_BOARD "devkit_480")
endif()

idf_component_register(
  SRCS ${NEV_BOARD_ESP_SRCS}
  INCLUDE_DIRS include ${CMAKE_CURRENT_LIST_DIR}/../../boards/${NEVOS_BOARD}
  REQUIRES nev_port driver esp_lcd
)
