# ESP-IDF component manifest for the apps. The host build reads sources.cmake
# directly; this exists so `apps` is a component the firmware can require.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/game_engine/sources.cmake)

#
# WHOLE_ARCHIVE is not an optimisation setting here, it is the thing that makes
# the apps exist.
#
# An app's only external symbol is the constructor that registers it, and
# nothing references that constructor by name — which is exactly what keeps
# "adding an app is adding a folder" true. A linker doing its normal job pulls
# object files out of an archive only when something needs a symbol from them,
# so without this the firmware links cleanly and boots to an empty home screen.
#
# The host build says the same thing as $<LINK_LIBRARY:WHOLE_ARCHIVE,...>.
#
idf_component_register(
  SRCS ${NEV_APP_SRCS} ${NEV_GAME_SRCS}
  INCLUDE_DIRS
    ${NEV_APP_COMMON_INCLUDE_DIRS}
    ${NEV_GAME_INCLUDE_DIRS}
  PRIV_INCLUDE_DIRS ${NEV_GAME_RULE_INCLUDE_DIRS}
  REQUIRES nev_appkit nev_services nev_bridge nev_persona nev_kernel lvgl
  WHOLE_ARCHIVE
)
