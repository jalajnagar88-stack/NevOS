/*
 * SDL2 display backend: a real 480x480 window.
 *
 * Mouse maps to touch and the keyboard maps to the two physical buttons, so the
 * simulator exercises the same nev_board_* reads the device will.
 */
#include "sim_backend.h"
#include "nev_port/nev_log.h"
#include <SDL2/SDL.h>

#define TAG "sdl"

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static const uint16_t *s_fb;
static int s_w, s_h;

nev_err_t sim_backend_init(const uint16_t *fb, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        NEV_LOGE(TAG, "SDL_Init failed: %s", SDL_GetError());
        return NEV_ERR_INVALID_STATE;
    }
    s_fb = fb;
    s_w = width;
    s_h = height;

    s_window = SDL_CreateWindow("NEVOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                height, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_window) {
        NEV_LOGE(TAG, "window creation failed: %s", SDL_GetError());
        return NEV_ERR_INVALID_STATE;
    }

    s_renderer =
        SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    if (!s_renderer) {
        NEV_LOGE(TAG, "renderer creation failed: %s", SDL_GetError());
        return NEV_ERR_INVALID_STATE;
    }

    /* RGB565 straight through: the texture format matches the device's pixel
     * format, so the simulator shows the same colour quantisation the panel will. */
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                  width, height);
    if (!s_texture) {
        NEV_LOGE(TAG, "texture creation failed: %s", SDL_GetError());
        return NEV_ERR_INVALID_STATE;
    }

    NEV_LOGI(TAG, "window %dx%d open — mouse drives touch, A/B keys drive the buttons", width,
             height);
    return NEV_OK;
}

void sim_backend_deinit(void) {
    if (s_texture) SDL_DestroyTexture(s_texture);
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window) SDL_DestroyWindow(s_window);
    s_texture = NULL;
    s_renderer = NULL;
    s_window = NULL;
    SDL_Quit();
}

void sim_backend_present(void) {
    if (!s_texture) return;
    SDL_UpdateTexture(s_texture, NULL, s_fb, s_w * (int)sizeof(uint16_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

static uint8_t s_button_mask;

static void set_button(SDL_Keycode key, bool down) {
    uint8_t bit = 0;
    if (key == SDLK_a)
        bit = NEV_BTN_A;
    else if (key == SDLK_b)
        bit = NEV_BTN_B;
    if (!bit) return;
    s_button_mask = down ? (uint8_t)(s_button_mask | bit) : (uint8_t)(s_button_mask & ~bit);
    nev_board_sim_inject_buttons(s_button_mask);
}

void sim_backend_poll(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                nev_board_sim_request_quit();
                break;

            case SDL_MOUSEBUTTONDOWN:
                nev_board_sim_inject_touch((int16_t)e.button.x, (int16_t)e.button.y,
                                           NEV_TOUCH_DOWN);
                break;
            case SDL_MOUSEBUTTONUP:
                nev_board_sim_inject_touch((int16_t)e.button.x, (int16_t)e.button.y, NEV_TOUCH_UP);
                break;
            case SDL_MOUSEMOTION:
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    nev_board_sim_inject_touch((int16_t)e.motion.x, (int16_t)e.motion.y,
                                               NEV_TOUCH_MOVE);
                }
                break;

            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE)
                    nev_board_sim_request_quit();
                else
                    set_button(e.key.keysym.sym, true);
                break;
            case SDL_KEYUP:
                set_button(e.key.keysym.sym, false);
                break;

            default:
                break;
        }
    }
}

nev_sim_display_kind_t sim_backend_kind(void) {
    return NEV_SIM_DISPLAY_SDL2;
}
