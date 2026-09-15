# See components/nev_port/sources.cmake for why this file exists.
set(NEV_SERVICES_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_SERVICES_SRCS
  ${NEV_SERVICES_DIR}/src/display_service.c
)

set(NEV_SERVICES_INCLUDE_DIRS ${NEV_SERVICES_DIR}/include)
