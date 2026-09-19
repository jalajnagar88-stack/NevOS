# ESP-IDF component manifest. The host build reads sources.cmake directly.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

# lwip because the transport is BSD sockets: the whole reason nev_ws and
# nev_mdns are portable C rather than esp_websocket_client is that the same
# code then runs under the simulator, and ESP-IDF's lwIP provides the socket
# API under the standard headers.
idf_component_register(
  SRCS ${NEV_BRIDGE_CODEC_SRCS} ${NEV_BRIDGE_TRANSPORT_SRCS}
  INCLUDE_DIRS include
  REQUIRES nev_port nev_kernel lwip
)
