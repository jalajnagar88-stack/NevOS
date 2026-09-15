# ADR 0001 — A port layer for dual-target builds

**Status:** accepted (M1) · **Supersedes:** nothing

## Context
The same application code must compile for `esp32s3` and for a host simulator,
and a feature demonstrable only on hardware is in the wrong layer.

## Options
1. **`nev_port` shim** — a ~30-function abstraction over tasks, sync, time,
   memory and logging. Nothing above L0 includes `freertos/*` or `esp_*`.
2. **ESP-IDF `linux` target** — `idf.py --preview set-target linux`.
3. **Fake ESP-IDF headers on the host** — stub `esp_log.h` etc.

## Decision
Option 1.

## Consequences
Unit tests link no RTOS and run in milliseconds. The simulator builds with
cmake, ninja and SDL2 alone — no ESP-IDF install to open the window. The cost is
one indirection and a CI lint (`tools/ci/lint_layers.py`) to keep the rule
honest; without the lint the abstraction would erode within a month.

Option 2 was rejected because it forces a full ESP-IDF install on every
contributor just to run the simulator, is preview-quality in 5.2, and makes
LVGL's SDL integration awkward. Option 3 was rejected because stubs drift, and
"it compiled on host" stops meaning anything.

Priority and core pinning are deliberately ignored on the host: pretending
pthreads is FreeRTOS would let timing bugs hide behind a simulation that does
not match the device. Timing is verified on hardware at M5.
