# ADR 0004 — Apps run on the render task

**Status:** accepted (M1)

## Context
LVGL 9 is not thread-safe. Apps draw widgets. Something has to decide where app
code runs.

## Options
1. **One task for rendering and app logic.**
2. **Separate app task with a global LVGL lock.**

## Decision
Option 1. `nev_ui` on core 1 runs `lv_timer_handler`, the persona renderer, and
app `on_event`/`on_tick`.

## Consequences
No lock, no contention, no deadlock class. Apps are held to a contract instead:
**`on_event` and `on_tick` must return in under 5 ms**, and anything slower
posts a job to `nev_work` and handles the completion event on a later frame.
`display_service` instruments every frame and logs the overrun with the frame
number.

The consequence to own: a badly written app can stutter the face. That is
visible immediately and attributable, and the lifecycle manager can force-close
a repeat offender. Option 2 makes the same bug invisible and intermittent, which
is worse.
