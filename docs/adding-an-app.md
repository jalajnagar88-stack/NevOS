# Adding an app

> One page, start to finish. If something here is wrong or missing, fixing this
> page is part of the change.

## 1. Make a folder

```
apps/games/pong/
  pong_app.c
```

Category decides where it lands on the home grid: `apps/games/`,
`apps/productivity/`, `apps/system/`.

## 2. Write the descriptor

```c
#include "nev_appkit/app.h"
#include "nev_appkit/ui_kit.h"

static nev_err_t pong_launch(lv_obj_t *root) {
    nev_ui_label(root, "Pong", NEV_FONT_TITLE, NEV_COL_INK);
    return NEV_OK;
}

static const nev_app_desc_t kPongApp = {
    .id               = "pong",          /* stable, persisted — never rename */
    .name             = "Pong",
    .icon             = LV_SYMBOL_PLAY,
    .category         = NEV_APP_CAT_GAME,
    .memory_budget_kb = 32,
    .requires_bridge  = false,
    .on_launch        = pong_launch,
};

NEV_APP_REGISTER(kPongApp);
```

## 3. Add the file to the build

One line in `apps/sources.cmake`. That list says which translation units to
compile; it does **not** name apps and nothing looks an app up in it. There is
no central registry to edit and nothing to forget.

## 4. Run it

```bash
./tools/build.sh sim -- --app pong
```

That's the whole procedure.

---

## The contract

**You get `root`.** A full-size container the shell owns. Build inside it. Do
not touch `lv_screen_active()`, the status bar, or another app's objects.

**`on_event` and `on_tick` run on the render task and must return in under
5 ms.** There is one render task and it owns a 33 ms budget
(ARCHITECTURE.md §4). Anything slower posts a job and handles the completion
event on a later frame. `display_service` logs an overrun with your app's name,
so this is found in development rather than in a review.

**No `nev_sleep_ms`, no busy-waits.** CI rejects them under `apps/`.

**No colour literals.** Use the tokens in `nev_appkit/theme.h`. CI rejects hex
colours under `apps/`. An app that defines its own palette produces a device
that looks like twelve different devices.

**Talk over the bus.** Your app may call *downward* — appkit, the kernel, the
persona — but must not reach sideways into another app or into a peer L3
module. Publish an event instead. See `docs/event-bus.md`.

**Work offline if you can.** ADR 0008: most of NEVOS is useful without the
companion daemon. Set `requires_bridge` only if your app genuinely cannot
function without it; the shell greys the tile out and says why, rather than
letting it launch and fail.

---

## Lifecycle callbacks

Only `on_launch` is required.

| Callback | When | What to do |
|---|---|---|
| `on_launch(root)` | first shown | build your UI under `root` |
| `on_suspend` | foreground lost | stop timers, release anything expensive |
| `on_resume` | foreground regained | restart timers; your objects still exist |
| `on_close` | being torn down | release blobs, files, subscriptions — **not** your LVGL objects |
| `on_event` | foreground only | handle a bus event, fast |
| `on_tick(now_ms)` | foreground only, once per frame | animate, fast |

`on_close` does **not** need to delete anything under `root`: the shell deletes
the container, and LVGL takes the children with it. Release only what the shell
cannot see.

## If your app has rules, put them in a core

Anything with interesting behaviour — game rules, a state machine, a scoring
system — belongs in a `_core.c` with **no LVGL, no clock of its own, and
randomness injected as a callback**, exactly as the games and the persona do.

The payoff is not tidiness. It is that the behaviour becomes assertable without
a display: `./tools/build.sh test` covers Snake's tail-cell rule and Breakout's
speed conservation in milliseconds, and neither is reachable by clicking around
in the simulator. See ADR 0011.

```c
/* rules: no LVGL, no clock, no global RNG */
bool  thing_core_step(thing_core_t *t, float dt);
void  thing_core_seed(thing_core_t *t, uint32_t (*rand_fn)(void *, uint32_t), void *ctx);
```

Add the core to `NEV_GAME_RULE_SRCS` in `apps/sources.cmake` and its test to
`NEV_GAME_RULE_TEST_SRCS`; the test build links it without a UI toolkit.

## Persisting something

Add a row to `NEV_SETTING_LIST` in `nev_kernel/nev_store.h` with a type, a
default and a range, then:

```c
nev_store_set_num(NEV_SET_HS_PONG, score);
uint32_t best = nev_store_num(NEV_SET_HS_PONG);
```

Writes are clamped to the declared range and written back on a debounce, so a
dragged slider is one flash write rather than sixty. Never invent a key
elsewhere: a setting that exists only as a string literal in the app that wrote
it is one nobody can find, validate, reset or migrate.
