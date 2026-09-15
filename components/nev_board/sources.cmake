# See components/nev_port/sources.cmake for why this file exists.
set(NEV_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_BOARD_SIM_SRCS ${NEV_BOARD_DIR}/src/sim/board_sim.c)
set(NEV_BOARD_SIM_SDL_SRCS ${NEV_BOARD_DIR}/src/sim/backend_sdl.c)
set(NEV_BOARD_SIM_HEADLESS_SRCS ${NEV_BOARD_DIR}/src/sim/backend_headless.c)

set(NEV_BOARD_ESP_SRCS ${NEV_BOARD_DIR}/src/esp32s3/board_esp32s3.c)

# board_config.h is resolved from boards/<name>/, selected by NEVOS_BOARD.
set(NEV_BOARD_INCLUDE_DIRS ${NEV_BOARD_DIR}/include)
