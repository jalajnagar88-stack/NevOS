include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

idf_component_register(
  SRCS ${NEV_APPKIT_SRCS}
  INCLUDE_DIRS include
  REQUIRES nev_port nev_kernel lvgl
)
