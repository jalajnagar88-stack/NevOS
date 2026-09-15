# See components/nev_port/sources.cmake for why this file exists.
set(NEV_KERNEL_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_KERNEL_SRCS
  ${NEV_KERNEL_DIR}/src/nev_events.c
  ${NEV_KERNEL_DIR}/src/nev_bus.c
  ${NEV_KERNEL_DIR}/src/nev_blob.c
)

set(NEV_KERNEL_INCLUDE_DIRS ${NEV_KERNEL_DIR}/include)

set(NEV_KERNEL_TEST_SRCS
  ${NEV_KERNEL_DIR}/test/test_events.c
  ${NEV_KERNEL_DIR}/test/test_bus.c
  ${NEV_KERNEL_DIR}/test/test_blob.c
  ${NEV_KERNEL_DIR}/test/test_mem.c
)
