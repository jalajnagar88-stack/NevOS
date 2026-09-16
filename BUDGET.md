# NEVOS Budget

Flash, PSRAM, internal SRAM and worst-case frame time, updated at every
milestone. The rule from ARCHITECTURE.md stands: if a feature does not fit, the
feature gets cut.

**Read the "measured where" column before trusting any number.** Host timings
say almost nothing about the device — an x86 laptop renders LVGL one to two
orders of magnitude faster than an ESP32-S3 at 240 MHz reading from PSRAM.
Timing numbers only become real at M5.

---

## M1 — kernel, event bus, simulator

### Internal SRAM (512 KB on the ESP32-S3; the scarce resource)

| Item | Bytes | Measured where | Note |
|---|---|---|---|
| Bus subscriber rings | 17,536 | both (static, `sizeof`) | 16 slots x 32 events x 32 B + headers |
| LVGL draw strips | 115,200 | both (2 x 480x60 RGB565) | the §5 strategy; DMA-capable |
| LVGL heap (`LV_MEM_SIZE`) | 262,144 | configured ceiling | bounded on purpose, not a clib heap |
| **Committed so far** | **394,880** | | task stacks and Wi-Fi not yet added |

This is already tight, and it is the number to watch. Wi-Fi and BLE want
roughly 40–50 KB of internal memory once `net_service` exists at M5, and the
task stacks in ARCHITECTURE.md §4 add about 42 KB. The lever, if it comes to
one, is `LV_MEM_SIZE`: 256 KB is generous for the shell NEVOS actually draws,
and can likely drop to 128 KB. A second lever is the strip height — 480x40
instead of 480x60 saves 38 KB at the cost of more flush calls per frame.

### PSRAM (8 MB; plentiful, slow)

| Item | Bytes | Note |
|---|---|---|
| Framebuffer | 460,800 | 480x480 RGB565, single, scanned out by the LCD peripheral |
| Blob pool | 344,064 | 32x512 B + 16x4096 B + 4x64 KB |
| **Committed so far** | **804,864** | 9.6% of 8 MB |

Comfortable. The blob pool counts are provisional; audio at M6 will tell us
whether 16 medium blocks is right for 20 ms frames at 50/s.

### Flash (16 MB, A/B halved)

| Partition | Size | Note |
|---|---|---|
| `ota_0` / `ota_1` | 5 MB each | the app budget; A/B is what halves it |
| `assets` (LittleFS) | 5.8 MB | fonts, sounds, sprites |
| `nvs` + `otadata` + `phy` + `coredump` | ~100 KB | |

**Application size: not yet measured.** It requires the ESP-IDF build, which
needs the Xtensa toolchain. Fill this in at the first device build.

### Frame time

| Measurement | Value | Measured where |
|---|---|---|
| Budget at 30 fps | 33,333 us | target |
| Average frame | 144 us | **host x86, headless** |
| Worst frame | 998 us | **host x86, headless** |
| Frames over budget | 0 of 120 | **host x86, headless** |
| Measured rate | 29.8 fps | host, paced |

Host numbers, and they prove only that the render path works and the pacing is
correct. Treat the device figure as unknown until M5. The arithmetic in
ARCHITECTURE.md §5 is the current best estimate of what will actually be hard:
27.6 MB/s of sustained PSRAM read for panel scanout alone, before drawing.

Partial refresh is confirmed working: 120 frames wrote 332,013 pixels, about
1.2% of the 27.6 M pixels a full redraw every frame would have cost. That ratio
is the single most important reason to expect 30 fps to be reachable at all.

---

## M2 — persona

### Internal SRAM — unchanged

The persona adds no static allocation of its own. `nev_persona_core_t` is one
static instance of 232 bytes; the face's LVGL objects come from the existing
`LV_MEM_SIZE` pool.

| Item | Bytes | Note |
|---|---|---|
| `nev_persona_core_t` | 232 | one static instance |
| Face LVGL objects | ~2 KB | 2 eyes x (rect + 2 lids + glint + squint arc), from the LVGL pool |
| **Committed total** | **~397 KB** | of 512 KB |

### Frame time

| Measurement | Boot screen | Persona | Measured where |
|---|---|---|---|
| Average frame | 144 us | 1,084 us | **host x86, headless** |
| Worst frame | 998 us | 2,392 us | **host x86, headless** |
| Frames over budget | 0 of 120 | 0 of 700 | **host x86, headless** |

The face costs roughly **7x** the boot screen per frame. That ratio is the
number worth carrying forward, because it is the part that transfers to the
device even though the absolute microseconds do not. Two things drive it:
rotating the eyes forces LVGL to allocate and composite a transform layer per
eye, and the happy squint cross-fades two overlapping shapes for about three
frames per transition.

Both are affordable at 30 fps on x86 with enormous headroom. Neither is
obviously affordable on an ESP32-S3 at 240 MHz rendering from PSRAM, and this
is the single biggest open question going into M5. The levers, in order:

1. Drop eye rotation below a slant threshold — most moods use little or none,
   and skipping the transform entirely when `rotation == 0` is free.
2. Shorten the cross-fade band so fewer frames draw both shapes.
3. Reduce the number of primitives per eye.

### A performance finding worth recording

An earlier version of the vector renderer rotated each lid as a *sibling* of the
eye rather than as a child. That forced LVGL to allocate a transform layer the
size of each lid ellipse — larger than `LV_MEM_SIZE` — which sent it into a
subdivision path that took **over seven minutes to render six frames**.

The same work as clipped children of the eye, inheriting its rotation, takes
**207 ms**. The lesson generalises past this renderer: on this platform, a
transform layer that does not fit in the LVGL pool is not merely slow, it is
pathological. Any future rotated or scaled object should be checked against
`LV_MEM_SIZE` before it ships.

---

## M3 — shell, settings, clock

### Internal SRAM

| Item | Bytes | Note |
|---|---|---|
| Settings store | 1,216 | 16 settings x (uint32 + 64-byte string + flag) |
| App registry | 192 | 24 descriptor pointers |
| Shell live slots | ~80 | 3 slots |
| **New this milestone** | **~1.5 KB** | |
| **Committed total** | **~398 KB** | of 512 KB |

Negligible against the framebuffer strips and the LVGL pool. The app framework
costs almost nothing in static memory; what it costs is LVGL objects, which come
out of `LV_MEM_SIZE` and scale with how many apps are kept warm.

`NEV_SHELL_MAX_LIVE` is 3 — one foreground and two suspended — precisely so
that number stays bounded.

### Frame time

| Screen | Average | Worst | Measured where |
|---|---|---|---|
| Shell (home grid) | 9 us | 831 us | **host x86, headless** |
| Settings | 17 us | 1,508 us | **host x86, headless** |
| Clock | 23 us | 1,627 us | **host x86, headless** |
| Persona face | 1,084 us | 2,392 us | **host x86, headless** |

The shell is nearly free, because a static grid of tiles redraws nothing
between frames — LVGL's dirty-rectangle tracking does exactly what ADR 0005
assumes it will. The clock costs more than settings only because its ring
advances every second.

**The face remains the expensive screen by two orders of magnitude**, and it is
still the number to carry into M5. Everything the shell adds is noise beside it.

The clock repaints on a second boundary rather than per frame. At 30 fps the
naive version would redraw an unchanged string 29 times a second, and every one
of those dirties a rectangle that then has to be flushed to PSRAM.

### Flash

Still not measured: the ESP-IDF build needs the Xtensa toolchain, which this
environment's network policy cannot reach. First real number comes from a
device build.

---

## M4 — game engine and five games

### Frame time

| Screen | Average | Worst | Tick rate | Measured where |
|---|---|---|---|---|
| Shell (home grid) | 9 us | 831 us | — | **host x86, headless** |
| Snake | 70 us | 1,893 us | 60 Hz | **host x86, headless** |
| Match | 146 us | 2,110 us | 60 Hz | **host x86, headless** |
| Runner | 177 us | 1,348 us | 120 Hz | **host x86, headless** |
| Reflex | 227 us | 1,768 us | 120 Hz | **host x86, headless** |
| Persona face | 1,084 us | 2,392 us | — | **host x86, headless** |
| **Breakout** | **934 us** | 1,707 us | 120 Hz | **host x86, headless** |

Breakout is the expensive game, by roughly five times. Two causes: a 120 Hz
fixed tick (a fast ball against a thin paddle can tunnel through both at 60 Hz),
and 40 brick objects whose styles are touched every redraw. If it does not
survive the device, the cheap fix is to stop restyling unchanged bricks — only
the one that was hit changes — which is a few lines and should recover most of
it.

Breakout and the persona face together would be about 2 ms per frame on the
host. That is comfortable at 30 fps here and is the pair to measure first on
real hardware.

### Memory

| Item | Bytes | Note |
|---|---|---|
| Particle pool | ~32 LVGL objects | preallocated per game, reused round-robin |
| Snake segments | 72 objects | one per possible body segment |
| Breakout bricks | 40 objects | plus 4 powerups and a ball |
| Match tiles | 49 objects | the largest single board |
| Game rule state | < 1 KB each | plain structs, no allocation |

All of it comes from `LV_MEM_SIZE` (256 KB) rather than static memory, and only
one game is ever live plus at most two suspended (`NEV_SHELL_MAX_LIVE`). Match
is the heaviest board at 49 tiles.

No game allocates during play. Particles are drawn from a fixed pool and a burst
during a burst steals the oldest rather than allocating.

---

## M6 — bridge and daemon

The daemon runs on the user's computer and costs the device nothing. What is
charged here is the device's half: a WebSocket client, an mDNS resolver, and the
buffers they need.

### Internal SRAM

| Item | Bytes | Measured where | Note |
|---|---|---|---|
| WebSocket receive buffer | 2,048 | `sizeof(nev_ws_t)` | caps an inbound frame; the largest the daemon sends is a 512-byte agent token |
| WebSocket send buffer | 4,352 | same | one whole outbound frame: a 4,096-byte audio chunk plus CBOR, the length prefix and a 14-byte header |
| Bridge encode scratch | 4,300 | `sizeof` | the audio chunk again, before framing |
| mDNS packet buffer | 600 | `sizeof(nev_mdns_t)` | one datagram; discovery stops once a daemon is found |
| Shell subscriber ring | +384 | depth 12 → 24 | see below |
| **Bridge total** | **~11.7 KB** | | one static instance, no allocation anywhere |
| **Committed total** | **~409 KB** | | of 512 KB |

The two 4 KB buffers exist for audio, which is the only large thing the device
sends. If internal SRAM gets tight during hardware bring-up, the audio chunk
size is the knob: halving it to 2,048 bytes costs one more frame per 64 ms of
speech and gives back 4 KB.

### Why the shell's queue grew

A streamed reply arrives as one event per token. A local model can produce a
burst of them between two frames, and at depth 12 the first half of a reply was
being evicted before the shell ever drew it — the screen showed the end of a
sentence with no beginning, with nothing logged, because DROP_OLDEST is exactly
what the shell asked for.

Depth 24 holds a whole short reply, at 768 bytes rather than 384. The other half
of the fix costs nothing: the bridge publishes at most six messages per poll and
leaves the rest in the socket, where TCP's own receive window holds the backlog.
That is a much better place for it than a ring of 32-byte slots on a device with
512 KB of RAM.

### Frame time

The bridge does not run on the render task. On the device it belongs to
`nev_net`; in the simulator it is polled from the main loop, and a 600-frame run
with discovery, pairing and an agent turn averaged 9 us per frame with a worst
case of 1,586 us — the worst being an LVGL relayout, not the bridge.

Non-blocking is what makes that true, and it is enforced by construction: every
socket call in `nev_port/nev_net.h` returns immediately, and a connection in
progress is polled rather than waited on.

---

## Method

Internal and PSRAM figures come from `sizeof` and from the allocation sites,
which the host allocator charges against simulated ESP32-S3 budgets
(`NEV_SIM_INTERNAL_BYTES`, `NEV_SIM_PSRAM_BYTES`) so that exhaustion reproduces
on a laptop. Frame times come from `display_service`, which instruments every
frame and reports the worst since init.

Reproduce with:

    ./tools/build.sh headless 120
