# Apps register themselves with a constructor, so this list exists only to tell
# the build which translation units to compile — not which apps exist. Adding an
# app means adding a folder and one line here; nothing references it by name.
set(NEV_APPS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_APP_SRCS
  ${NEV_APPS_DIR}/games/snake/snake_app.c
  ${NEV_APPS_DIR}/games/snake/snake_core.c
  ${NEV_APPS_DIR}/system/settings/settings_app.c
  ${NEV_APPS_DIR}/system/clock/clock_app.c
)

# Game rules live in <game>_core.c with no LVGL dependency, so they can be
# tested without a display. The renderers stay in <game>_app.c.
set(NEV_GAME_RULE_SRCS
  ${NEV_APPS_DIR}/games/snake/snake_core.c
)
set(NEV_GAME_RULE_INCLUDE_DIRS
  ${NEV_APPS_DIR}/games/snake
)
set(NEV_GAME_RULE_TEST_SRCS
  ${NEV_APPS_DIR}/games/snake/test/test_snake_core.c
)
