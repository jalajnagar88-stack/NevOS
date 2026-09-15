# See components/nev_port/sources.cmake for why this file exists.
set(NEV_APPKIT_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_APPKIT_SRCS
  ${NEV_APPKIT_DIR}/src/theme.c
  ${NEV_APPKIT_DIR}/src/app_registry.c
  ${NEV_APPKIT_DIR}/src/ui_kit.c
  ${NEV_APPKIT_DIR}/src/shell.c
)

set(NEV_APPKIT_INCLUDE_DIRS ${NEV_APPKIT_DIR}/include)

set(NEV_APPKIT_TEST_SRCS
  ${NEV_APPKIT_DIR}/test/test_app_registry.c
)
