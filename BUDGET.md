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

## Method

Internal and PSRAM figures come from `sizeof` and from the allocation sites,
which the host allocator charges against simulated ESP32-S3 budgets
(`NEV_SIM_INTERNAL_BYTES`, `NEV_SIM_PSRAM_BYTES`) so that exhaustion reproduces
on a laptop. Frame times come from `display_service`, which instruments every
frame and reports the worst since init.

Reproduce with:

    ./tools/build.sh headless 120
