# ADR 0005 — SRAM strip buffers, PSRAM scanout framebuffer

**Status:** accepted (M1) · **Revisit:** M5, with measurements

## Context
A 480x480 RGB565 framebuffer is 460,800 bytes. The ESP32-S3 LCD peripheral
scans it out of PSRAM continuously: ~27.6 MB/s of sustained read at 60 Hz
before NEVOS draws anything, against roughly 40–80 MB/s of octal PSRAM shared
with the CPU.

## Options
1. **Double-buffered full framebuffers in PSRAM**, LVGL rendering into them.
2. **One PSRAM framebuffer for scanout, LVGL rendering into SRAM strips**,
   flush_cb copying across; partial refresh by default.

## Decision
Option 2. Two 480x60 strips, 57,600 bytes each, in internal DMA-capable memory.

## Consequences
The CPU and the LCD DMA stop competing for the same PSRAM reads. Partial
refresh means the face — the only thing moving most of the time — costs a
fraction of a frame: measured at M1, 120 frames wrote 332,013 pixels, about 1.2%
of a full redraw every frame.

**What this rules out:** full-screen 30 fps animation is not in budget, and no
feature may assume one. Screen transitions are designed as bounded slides and
fades rather than full-frame crossfades. Stating this at M1 rather than
discovering it at M5 is the entire point of the ADR.

115 KB of internal SRAM is a real cost against a 512 KB budget. If Wi-Fi and the
task stacks squeeze it at M5, the levers in order are `LV_MEM_SIZE` (256 KB is
generous) and strip height (480x40 saves 38 KB).
