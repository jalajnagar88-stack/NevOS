# See components/nev_port/sources.cmake for why this file exists.
set(NEV_BRIDGE_DIR ${CMAKE_CURRENT_LIST_DIR})

# The codec has no LVGL and no networking, so it joins the toolkit-free core
# that unit tests link against. The transport does not.
set(NEV_BRIDGE_CODEC_SRCS
  ${NEV_BRIDGE_DIR}/src/nev_cbor.c
  ${NEV_BRIDGE_DIR}/src/nev_proto.c
)

set(NEV_BRIDGE_INCLUDE_DIRS ${NEV_BRIDGE_DIR}/include)

set(NEV_BRIDGE_TEST_SRCS
  ${NEV_BRIDGE_DIR}/test/test_cbor.c
  ${NEV_BRIDGE_DIR}/test/test_proto.c
)
