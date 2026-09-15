# Apps register themselves with a constructor, so this list exists only to tell
# the build which translation units to compile — not which apps exist. Adding an
# app means adding a folder and one line here; nothing references it by name.
set(NEV_APPS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_APP_SRCS
  ${NEV_APPS_DIR}/system/settings/settings_app.c
  ${NEV_APPS_DIR}/system/clock/clock_app.c
)
