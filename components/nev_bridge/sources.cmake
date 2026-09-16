# See components/nev_port/sources.cmake for why this file exists.
set(NEV_BRIDGE_DIR ${CMAKE_CURRENT_LIST_DIR})

# The codec has no LVGL and no networking, so it joins the toolkit-free core
# that unit tests link against. The transport does not.
set(NEV_BRIDGE_CODEC_SRCS
  ${NEV_BRIDGE_DIR}/src/nev_cbor.c
  ${NEV_BRIDGE_DIR}/src/nev_proto.c
)

# The transport: sockets, but still no LVGL and no RTOS, so it links into the
# test suite too. That is the whole reason it is portable C rather than
# esp_websocket_client.
set(NEV_BRIDGE_TRANSPORT_SRCS
  ${NEV_BRIDGE_DIR}/src/nev_sha1.c
  ${NEV_BRIDGE_DIR}/src/nev_ws.c
  ${NEV_BRIDGE_DIR}/src/nev_mdns.c
  ${NEV_BRIDGE_DIR}/src/nev_bridge.c
)

set(NEV_BRIDGE_INCLUDE_DIRS ${NEV_BRIDGE_DIR}/include)

set(NEV_BRIDGE_TEST_SRCS
  ${NEV_BRIDGE_DIR}/test/test_cbor.c
  ${NEV_BRIDGE_DIR}/test/test_proto.c
  ${NEV_BRIDGE_DIR}/test/test_ws.c
  ${NEV_BRIDGE_DIR}/test/test_mdns.c
)
