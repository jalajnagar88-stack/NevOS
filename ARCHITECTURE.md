# NEVOS Architecture

> Status: **M1–M4 complete.** Kernel, persona, shell and the five games are
> implemented and tested on the simulator. M5 onward — hardware bring-up, the
> bridge and the daemon — is still the plan this document serves.
> Every decision here is meant to survive a year of building on it. Where a
> decision is reversible, this document says so. Where it is not, it says that too.

NEVOS is an embedded operating system for a desktop companion robot: an
ESP32-S3 with a 480x480 touch panel, a face, five original games, focus and
capture tools, and a WebSocket link to an agent daemon on the user's PC.

This document defines the layering, the build targets, the task topology, and
the rules that keep the codebase from collapsing into a pile of special cases.
The event bus — the single mechanism by which subsystems talk — has its own
document: [`docs/event-bus.md`](docs/event-bus.md).

---

## 1. The three rules

Everything else in this document is a consequence of these.

**R1 — Downward calls only.** A layer may call the layer beneath it. It may
never call upward, and from L3 upward it may never call *sideways* either.
Persona does not call Appkit. Appkit does not call Persona. The `snake` app
does not call `audio_service`. They exchange events. The one-way dependency
graph is what makes the system testable, and what makes it possible to delete
or replace a layer without archaeology.

**R2 — Hardware is a detail.** Nothing above L0 knows whether it is running on
an ESP32-S3 or on a laptop. Nothing above L0 includes `driver/`, `esp_*`, or
`freertos/`. A feature that can only be demonstrated on hardware has been put
in the wrong layer, and the fix is to move it down, not to make an exception.

**R3 — Nothing blocks the frame.** There is one rendering task and it owns a
33 ms budget. Any operation that cannot state a bounded worst case inside that
budget runs on a worker and reports back as an event. There is no `delay()` in
application code, and no busy-wait anywhere.

These are enforced by CI, not by good intentions. See §9.

---

## 2. Layer map

```
                     ┌──────────────────────────────────────────────┐
  L5   apps/         │ snake breakout runner match reflex           │
                     │ focus notes meeting agent settings clock     │
                     └──────────────────────────────────────────────┘
                     ┌──────────────────────────────────────────────┐
  L4   nev_appkit    │ registry · lifecycle · navigation · UI kit   │
                     └──────────────────────────────────────────────┘
        ┌───────────────────────────────┐  ┌────────────────────────┐
  L3    │ nev_persona                   │  │ nev_bridge        (L6) │
        │ mood FSM · face renderer      │  │ CBOR codec · WS client │
        └───────────────────────────────┘  └────────────────────────┘
                     ┌──────────────────────────────────────────────┐
  L2   nev_services  │ display · audio · input · net · ota · power  │
                     └──────────────────────────────────────────────┘
                     ┌──────────────────────────────────────────────┐
  L1   nev_kernel    │ EVENT BUS · blobs · heap · store · log       │
                     └──────────────────────────────────────────────┘
                     ┌──────────────────────────────────────────────┐
  L0   nev_board     │ display flush · touch · audio io · imu · pwr │
                     └──────────────────────────────────────────────┘
                     ┌──────────────────────────────────────────────┐
  L-1  nev_port      │ task · queue · mutex · time · atomic · log   │
                     └──────────────────────────────────────────────┘
                        FreeRTOS / ESP-IDF          pthreads / SDL2
```

`nev_bridge` is drawn beside L3 rather than above it because it is a peer of
persona, not a consumer: it speaks to `net_service` below it and publishes
events. The brief numbers it L6; the number is a label, not a position.

### L-1 — `nev_port`, the platform shim

The brief did not ask for this layer. It is the thing that makes the rest of
the brief achievable, so it is here.

`nev_port` is a deliberately small surface — roughly 30 functions — over the
handful of OS primitives NEVOS actually uses:

| Header | Provides |
|---|---|
| `nev_port/task.h` | `nev_task_create(fn, name, stack, prio, core)`, yield, delay-until |
| `nev_port/queue.h` | fixed-item ring queue, blocking receive with timeout, ISR-safe send |
| `nev_port/mutex.h` | recursive mutex, critical section |
| `nev_port/time.h` | `nev_now_us()` (monotonic), `nev_now_ms()` |
| `nev_port/mem.h` | `nev_malloc(size, NEV_MEM_INTERNAL \| NEV_MEM_PSRAM \| NEV_MEM_DMA)` |
| `nev_port/atomic.h` | 32-bit atomics (C11 `stdatomic` on host, `esp_atomic` on device) |

Two implementations: `src/esp32s3/` (FreeRTOS, ESP-IDF heap caps) and
`src/host/` (pthreads, plain `malloc` with a PSRAM-budget accountant that
refuses allocations exceeding the simulated 8 MB, so PSRAM exhaustion is
reproducible on a laptop).

The payoff is concrete: **unit tests link no RTOS at all**, and the simulator
builds with `cmake` + `ninja` + SDL2 on a bare Mac — no ESP-IDF install needed
to open the window or run the test suite. The cost is one indirection and a CI
lint that rejects `esp_`/`freertos/` includes above L0.

### L0 — `nev_board`

The board abstraction. One header of capabilities:

```c
nev_err_t nev_board_init(void);
void      nev_board_display_flush(const nev_rect_t *area, const uint16_t *px);
bool      nev_board_touch_read(nev_touch_t *out);      // false = no touch
size_t    nev_board_audio_in_read(int16_t *dst, size_t frames);
size_t    nev_board_audio_out_write(const int16_t *src, size_t frames);
bool      nev_board_imu_read(nev_imu_sample_t *out);
uint8_t   nev_board_buttons_read(void);                 // bitmask
void      nev_board_power_state(nev_power_t *out);
void      nev_board_backlight_set(uint8_t percent);
```

Implementations: `board_esp32s3.c`, and `board_sim.c` with two swappable
display backends — **SDL2** (a real window, for the Mac) and **headless**
(renders into a buffer, dumps PNG on demand, drives a synthetic clock). The
headless backend is what lets CI and this development container assert that
the render path actually produced pixels, rather than merely compiling.

**`boards/<name>/board_config.h` is the only file in the repository permitted
to contain a GPIO number.** A CI lint greps for `GPIO_NUM_` and bare pin
integers outside it and fails the build. Adding the Waveshare or Elecrow
variant later means adding a directory, not editing drivers.

### L1 — `nev_kernel`

The event bus, and the four things the bus needs to be trustworthy:

- **`nev_bus`** — typed publish/subscribe, per-subscriber fixed-size rings,
  zero allocation on the hot path. Specified in `docs/event-bus.md`.
- **`nev_blob`** — ref-counted fixed-size buffer pool, preallocated at boot,
  for payloads too large for a 32-byte event (audio frames, transcript text,
  agent tokens). Handles travel in events; bytes never get copied through the
  bus.
- **`nev_heap`** — allocation wrappers tagged by owner, per-task stack
  high-water sampling, and a watchdog that publishes `SYS_MEM_PRESSURE` before
  the system is actually in trouble.
- **`nev_store`** — schema'd key/value settings (NVS on device, a JSON file on
  host) with declared defaults and types, plus the LittleFS asset mount.
- **`nev_log`** — levelled logging into a ring buffer, drained to serial and,
  later, to the daemon. The ring means a crash still has the last 8 KB of
  context.

### L2 — `nev_services`

Long-lived subsystems that own hardware and publish events. Each is a task or
a set of tasks; none of them expose a call-me-directly API to L3+.

- `display_service` — LVGL 9 init, the draw-buffer strategy in §5,
  frame-time instrumentation, the 30 fps budget.
- `audio_service` — 16 kHz mono capture, VAD, Opus encode; playback mixer for
  SFX and TTS.
- `input_service` — the *only* producer of `INPUT` events. Fuses touch,
  buttons, and IMU into one stream, including gesture recognition (shake, tap,
  tilt) so that no app ever parses raw accelerometer samples.
- `net_service` — Wi-Fi, BLE provisioning, exponential-backoff reconnect,
  mDNS discovery of the daemon.
- `ota_service` — signed HTTPS OTA, A/B partitions, rollback on failed boot.
- `power_service` — idle detection, dimming, light sleep, battery reporting.

### L3 — `nev_persona`

Eight moods (`idle`, `curious`, `happy`, `focused`, `sleepy`, `celebrating`,
`concerned`, `thinking`), a transition table, and a face renderer built from
LVGL canvas primitives — arcs, rounded rects, beziers — parameterised by a
`face_params_t` of roughly a dozen scalars (eye open, eye slant, pupil x/y,
brow angle, mouth curve, blush, head bob). Moods are *not* animation clips;
they are target parameter sets. Transitions are interpolation between them,
which is why they are tweened for free and why a mood change mid-transition
does not glitch.

Consumes events (shake, tap, idle timeout, game win, agent thinking, low
battery) and publishes `PERSONA_MOOD_CHANGED`. Exposes exactly one imperative
call, `nev_persona_set_mood(mood, intensity, hold_ms)`. An app or appkit calling
it is an L5/L4 to L3 call — downward, and therefore legal under R1. `nev_bridge`
may **not** call it, being a peer at L3; the daemon drives the face by
publishing `BRIDGE.MOOD_HINT`, which persona subscribes to.

The component splits in two so that the personality is testable: `persona_core`
holds the mood machine, the tween, blink timing and idle drift, and depends on
neither LVGL nor the bus nor any clock — `now_ms` is an argument. `persona`
binds it to both. See `docs/persona.md`.

### L4 — `nev_appkit`

- **Registry** — static registration via a linker-section macro:
  `NEV_APP_REGISTER(snake_app_desc)` places a descriptor pointer in a custom
  section that the registry walks at boot. Adding an app touches exactly one
  new folder; there is no central list to edit and therefore no merge conflict
  and no forgotten registration.
- **Lifecycle** — one foreground app; background apps suspended with a
  declared memory budget; forced close under `SYS_MEM_PRESSURE`, oldest and
  largest first.
- **Navigation** — home grid, swipe gestures, persistent status bar (time,
  battery, Wi-Fi, agent link).
- **UI kit** — theme tokens (color, radius, spacing, type scale) and the
  shared widgets. **No app defines a color literal.** CI lints for hex colors
  outside the theme file.

### L5 — `apps/`

Self-contained folders. Each has its own `CMakeLists.txt`, its own tests, and
a descriptor. The games additionally sit on a `game_engine` helper (fixed
timestep, sprite blit, collision, particles, screen shake, score persistence,
shared high-score table) so that five games do not mean five game loops.

**Originality is a hard constraint.** Every game is an original work
inspired by a genre. No emulation, no ROMs, no trademarked characters, level
layouts, sprites, or music. This is recorded in the architecture rather than
only in the prompt because it constrains implementation: for example, the
`match` app's scoring and cascade rules are specified from first principles in
its own design note, not derived from any existing title.

### L6 — `nev_bridge`

Length-prefixed CBOR over a device-initiated WebSocket. The wire schema lives
in exactly one place — `schema/nevos.toml` — and `tools/schema/gen.py` emits
both the C structs/codec and the Rust types. Neither side can drift because
neither side is hand-written. Round-trip and fuzz tests run in CI on both
languages against shared golden vectors.

The transport is portable C over a socket shim at L-1 (`nev_port/nev_net.h`)
rather than `esp_websocket_client` and ESP-IDF's mDNS component, which would
each have been one line of configuration. The reason is the second constraint in
the brief: a bridge built on device-only components can only be debugged on the
device, and a handshake, a frame codec, a reconnect policy and a pairing flow
are a great deal of logic to debug through a serial log. As written, the whole
bridge runs on the host and talks to the real daemon —
`./tools/build.sh bridge-live` does exactly that.

`nev_net_posix.c` is a single implementation for both targets: ESP-IDF's lwIP
serves the BSD socket API under the standard headers. The only differences are
link state, which the board reports, and lwIP's smaller socket count.

---

## 3. Build targets

Two roots, because ESP-IDF wants to own the top-level `CMakeLists.txt` and a
host build does not. One dispatching root would be clever and fragile.

```
targets/esp32s3/CMakeLists.txt   idf.py -C targets/esp32s3 build
targets/host/CMakeLists.txt      cmake -S targets/host -B build/host -GNinja
targets/tests/CMakeLists.txt     ctest --test-dir build/tests
```

All three pull the same `components/` and `apps/` trees. `tools/build.sh` wraps
them so the everyday commands are `./tools/build.sh sim` and
`./tools/build.sh test`.

The host target takes `-DNEVOS_DISPLAY=sdl2|headless`. Identical application
code; only the L0 backend differs.

---

## 4. Task topology

Core 1 is the UI core and is kept deliberately quiet. Core 0 carries
everything with unpredictable latency: radios, audio, filesystem.

| Task | Core | Prio | Stack | Period | Responsibility |
|---|---|---|---|---|---|
| `nev_ui` | 1 | 5 | 12 K | 33 ms | LVGL timer handler, persona render, app `on_event`, app `on_tick` |
| `nev_input` | 0 | 7 | 4 K | 10 ms | touch + button poll, IMU read, gesture fusion |
| `nev_audio_in` | 0 | 8 | 4 K | I2S DMA | capture, VAD, Opus encode |
| `nev_audio_out` | 0 | 7 | 4 K | I2S DMA | mixer, playback |
| `nev_net` | 0 | 4 | 8 K | event | Wi-Fi, WebSocket, bridge codec |
| `nev_work` | 0 | 3 | 6 K | job | generic worker: filesystem, crypto, OTA chunks |
| `nev_sys` | 0 | 2 | 4 K | 1 s | heap watchdog, log drain, power policy |

**`nev_ui` and the app task are the same task, on purpose.** LVGL 9 is not
thread-safe. Splitting rendering from app logic would require a global LVGL
lock held across every widget call, which buys contention and deadlock risk in
exchange for nothing. Instead, apps run on the render task and are held to a
contract: **`on_event` and `on_tick` must return in under 5 ms.** Anything
slower posts a job to `nev_work` and handles the completion event on a later
frame. `display_service` instruments every callback and logs a warning naming
the app when it overruns, so violations are found in development rather than
in a review.

Consequence worth stating plainly: a badly written app can stutter the face.
The instrumentation makes that visible immediately, and the lifecycle manager
can force-close a repeat offender. The alternative — a locked, threaded UI —
makes the same bug invisible and intermittent instead.

---

## 5. The rendering budget, honestly

This is the part of the brief most likely to disappoint, so it is stated up
front rather than discovered at M5.

A 480x480 RGB565 framebuffer is **460,800 bytes**. The panel scans out
continuously from PSRAM via the S3 LCD peripheral: at 60 Hz refresh that is
**27.6 MB/s of sustained read bandwidth** before NEVOS draws a single pixel.
Octal PSRAM at 80 MHz supplies on the order of 40–80 MB/s in practice, shared
with the CPU. Naively double-buffering full frames in PSRAM and having LVGL
render into them means the CPU and the LCD DMA fight over the same bus, and
the result is tearing and missed frames.

**The design instead is:**

- One framebuffer in PSRAM, scanned out by the LCD peripheral (`bounce`
  buffers enabled so the DMA reads through internal SRAM).
- LVGL renders into **two 480x60 draw buffers in internal SRAM** (57,600 bytes
  each, 115 KB total). These are fast to write and DMA-friendly.
- `flush_cb` copies each finished strip into the PSRAM framebuffer.
- **Partial refresh by default.** LVGL's dirty-rectangle tracking means the
  face — which occupies perhaps 40% of the screen and is the only thing moving
  most of the time — costs a fraction of a full frame.

With that, 30 fps sustained is a realistic target for the face and for the
games, which are deliberately low-fill designs. **A full-screen 30 fps
animation is not in budget** and no feature should assume one. Screen
transitions will be designed as slides and fades over limited areas rather
than full-frame crossfades.

`BUDGET.md` tracks the measured numbers per milestone. If a feature does not
fit, this document's position is that the feature gets cut.

---

## 6. Memory plan (preliminary)

| Region | Budget | Notes |
|---|---|---|
| Internal SRAM (512 KB) | ~115 KB LVGL draw buffers, ~60 KB task stacks, ~40 KB Wi-Fi/BLE, rest free | the scarce resource |
| PSRAM (8 MB) | 461 KB framebuffer, ~1 MB blob pool, ~2 MB LVGL objects + assets, rest headroom | plentiful, but slow |
| Flash (16 MB) | 2x 6 MB OTA app slots, 3 MB LittleFS assets, NVS + coredump | A/B halves the app budget |

The 6 MB per-slot app budget is the number to watch. LVGL, Opus, mbedTLS and
the Wi-Fi stack are each substantial. Opus encode in particular may need to be
dropped in favour of raw 16 kHz PCM over LAN — bandwidth is free on a local
network — and that trade is called at M6 with measurements, not now.

---

## 7. Data flow: one worked example

The `agent` app, end to end, showing that no module calls another directly:

```
user holds button A
  → nev_input publishes          INPUT_BUTTON_DOWN{A}
  → agent app (on_event)  publishes  AUDIO_CAPTURE_START
  → audio_service begins I2S capture
  → audio_service publishes      AUDIO_CHUNK{blob, 20ms}   (~50/s)
      ├→ nev_bridge encodes and sends over WebSocket
      └→ (release blob)
  → daemon transcribes locally, streams back
  → nev_bridge publishes         BRIDGE_TRANSCRIPT_PARTIAL{blob}
  → agent app renders the partial text
  → daemon streams LLM tokens
  → nev_bridge publishes         BRIDGE_AGENT_TOKEN{blob}   and
                                 PERSONA_MOOD_HINT{thinking}
  → persona transitions to `thinking`; agent app appends the token
  → daemon finishes; BRIDGE_AGENT_DONE
  → persona returns to `idle` after its dwell timer
```

`audio_service` has never heard of `nev_bridge`. `nev_persona` has never heard
of the `agent` app. Replacing the transport, or adding a second consumer of
audio (a local wake-word detector), is additive.

---

## 8. Security and privacy posture

- **No secrets in firmware.** The device holds a pairing-derived device token
  and nothing else. Cloud provider API keys live on the daemon, on the user's
  machine. There is no code path that would let the device hold one, and the
  bridge schema has no message type that could carry one.
- **Pairing** is a one-time six-digit code shown on the device screen and
  entered in the desktop app, yielding a long-lived device token. The token is
  stored in NVS; factory reset erases it.
- **Local-first capture.** The daemon's default transcription backend is local
  `whisper.cpp`. Meeting and note audio does not leave the machine unless the
  user explicitly opts into a cloud backend.
- **The recording indicator is not a UI nicety, it is an invariant.** Any
  active microphone capture forces a visible on-device indicator and a visible
  tray indicator, driven from the same state that gates the I2S peripheral —
  not from a parallel flag that could disagree with it.
- **OTA images are signed**; A/B partitions with automatic rollback on a failed
  boot.

---

## 9. Enforcement

A rule that is not checked is a rule that is already broken somewhere.
`tools/ci/lint_layers.py` runs in CI and fails the build on:

| Check | Rule |
|---|---|
| `esp_`/`freertos/`/`driver/` include above L0 | R2 |
| a component including a header from a higher layer | R1 |
| an L3+ component including another L3+ component | R1 |
| `GPIO_NUM_` or a pin macro outside `boards/*/board_config.h` | §L0 |
| a hex color literal outside the theme token file | §L4 |
| `vTaskDelay`, `sleep`, `usleep` in `apps/` | R3 |

Plus: `clang-format` verified, both targets built, host tests run, and
`-Wall -Wextra -Werror` on everything.

---

## 10. Decisions recorded

Each of these has an ADR in [`docs/adr/`](docs/adr/):

| # | Decision | Rejected alternative |
|---|---|---|
| 0001 | `nev_port` shim for dual-target | ESP-IDF `linux` preview target; fake ESP headers |
| 0002 | Per-subscriber queues, no central dispatcher | central queue + fan-out task |
| 0003 | Ref-counted blob pool for large payloads | 32-byte events only + side channels |
| 0004 | Apps run on the render task | separate app task + global LVGL lock |
| 0005 | SRAM strip buffers + PSRAM scanout framebuffer | double-buffered PSRAM framebuffers |
| 0006 | Two build roots under `targets/` | one dispatching root CMakeLists |
| 0007 | Schema-generated bridge codec | hand-written C and Rust structs |
| 0008 | Device fully useful offline | daemon assumed present |
| 0009 | Push-to-talk through M6 | on-device esp-sr wake word |
| 0010 | Eyes-only face | character face; abstract orb |
| 0011 | Game rules split from rendering | rules inside the renderer, smoke-tested |
| 0012 | Dependency-free generated codec, 3 implementations | a CBOR crate on the Rust side |
| 0013 | Local-only agent; no cloud backend written | hosted model behind an API key on the daemon |
| 0014 | Device link and control API on separate listeners | one server, both on the LAN |

---

## 11. Milestones

| | Deliverable | Runs on |
|---|---|---|
| M1 | skeleton, dual-target build, LVGL window, event bus + tests | simulator |
| M2 | persona: 8 moods, tweened transitions, idle behaviors | simulator |
| M3 | shell: appkit, home, status bar, UI kit, settings persistence | simulator |
| M4 | game engine + five games, high scores, sound | simulator |
| M5 | hardware bring-up: display, touch, audio, IMU, Wi-Fi, OTA | device |
| M6 | bridge schema + codegen, WebSocket, pairing, Rust daemon, `notes` + `agent` | both |
| M7 | meeting mode, power management, boot animation, release build, docs | both |

M1–M4 require no hardware. M6 is where the differentiation lives and is where
the engineering effort should concentrate.

---

## Appendix A — Repository layout

```
nevos/
├── ARCHITECTURE.md              this document
├── BUDGET.md                    flash / PSRAM / frame-time, updated each milestone
├── README.md                    build and run, for a new contributor
├── .clang-format  .gitignore  .editorconfig
│
├── docs/
│   ├── event-bus.md             the bus contract
│   ├── app-lifecycle.md         (M3)
│   ├── adding-an-app.md         (M3) — one page, start to finish
│   ├── protocol.md              (M6)
│   ├── daemon.md                (M6)
│   └── adr/0001..0007-*.md      decisions and what they cost
│
├── targets/                     the three build roots
│   ├── esp32s3/                 ESP-IDF project  · idf.py -C targets/esp32s3 build
│   │   ├── CMakeLists.txt  sdkconfig.defaults  partitions.csv
│   │   └── main/main.c
│   ├── host/                    simulator        · cmake -S targets/host -B build/host
│   │   ├── CMakeLists.txt
│   │   └── main.c               -DNEVOS_DISPLAY=sdl2|headless
│   └── tests/                   Unity, host-only · ctest --test-dir build/tests
│       └── CMakeLists.txt
│
├── boards/
│   └── devkit_480/board_config.h    THE ONLY FILE WITH GPIO NUMBERS
│
├── components/
│   ├── nev_port/                L-1  task queue mutex time mem atomic
│   │   ├── include/nev_port/*.h
│   │   └── src/{esp32s3,host}/
│   ├── nev_board/               L0   one impl per target
│   │   ├── include/nev_board/board.h
│   │   └── src/{esp32s3/, sim/{board_sim.c,display_sdl.c,display_headless.c}}
│   ├── nev_kernel/              L1   bus blob heap store log
│   │   ├── include/nev_kernel/{nev_bus.h,nev_events.h,nev_blob.h,nev_heap.h,nev_store.h,nev_log.h}
│   │   ├── src/
│   │   └── test/{test_bus.c,test_blob.c,test_store.c}
│   ├── nev_services/            L2   display audio input net ota power
│   ├── nev_persona/             L3   mood FSM + face renderer + test/
│   ├── nev_appkit/              L4   registry lifecycle nav ui_kit theme
│   └── nev_bridge/              L6   generated codec + ws client + test/
│
├── apps/
│   ├── game_engine/             shared: loop, sprites, collision, particles, scores
│   ├── games/{snake,breakout,runner,match,reflex}/
│   ├── productivity/{focus,notes,meeting,agent}/
│   └── system/{settings,clock}/
│       └── each: CMakeLists.txt · <app>.c · <app>_desc.c · test/
│
├── schema/nevos.proto.yaml      single source of truth for the wire protocol
├── tools/
│   ├── build.sh                 ./tools/build.sh {sim,test,device,lint,format}
│   ├── schema/gen.py            → C codec + Rust types
│   ├── ci/lint_layers.py        enforces §9
│   └── replay.py                feed a captured bus trace into the simulator
│
├── companion/                   (M6) Rust workspace
│   ├── Cargo.toml
│   ├── nevosd/                  tokio + axum WS server, mDNS, device registry
│   ├── nevos-proto/             generated types + codec
│   ├── nevos-stt/               trait + whisper.cpp backend + cloud backend
│   ├── nevos-agent/             trait + Anthropic client, streaming, tool use
│   └── nevos-tray/              Tauri tray app
│
└── .github/workflows/ci.yml     build both targets · tests · lint · format
```

Two structural choices worth noting:

- **Apps are folders, not entries in a list.** Adding one creates a directory
  and touches nothing else — the registration macro and a CMake glob do the
  rest. No central registry file means no merge conflicts and no app that
  someone forgot to register.
- **Tests live beside the code they test**, not in a parallel tree. A component
  without a `test/` directory is visibly missing one.
