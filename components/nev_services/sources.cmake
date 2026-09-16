# See components/nev_port/sources.cmake for why this file exists.
set(NEV_SERVICES_DIR ${CMAKE_CURRENT_LIST_DIR})

# The policy half of the power service: no board, no bus, no clock of its own,
# so the test suite can run four minutes of idling in a microsecond. Listed
# separately because the tests link it without the rest of L2.
set(NEV_SERVICES_CORE_SRCS
  ${NEV_SERVICES_DIR}/src/power_core.c
  ${NEV_SERVICES_DIR}/src/net_core.c
  ${NEV_SERVICES_DIR}/src/ota_core.c
)

set(NEV_SERVICES_SRCS
  ${NEV_SERVICES_CORE_SRCS}
  ${NEV_SERVICES_DIR}/src/display_service.c
  ${NEV_SERVICES_DIR}/src/input_service.c
  ${NEV_SERVICES_DIR}/src/power_service.c
  ${NEV_SERVICES_DIR}/src/audio_service.c
)

set(NEV_SERVICES_TEST_SRCS
  ${NEV_SERVICES_DIR}/test/test_power_core.c
  ${NEV_SERVICES_DIR}/test/test_net_core.c
  ${NEV_SERVICES_DIR}/test/test_ota_core.c
)

set(NEV_SERVICES_INCLUDE_DIRS ${NEV_SERVICES_DIR}/include)
