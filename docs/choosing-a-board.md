# Choosing a board

NEVOS needs four things from the hardware. Everything else is preference.

| | Why it is not negotiable |
|---|---|
| **ESP32-S3** | Two cores — one renders, one carries the radios and audio. The S2 has one and no Bluetooth; the C3 is a single RISC-V core with no PSRAM interface worth using. |
| **8 MB octal PSRAM** | The framebuffer is 460,800 bytes and the blob pool is another 344 KB. Quad PSRAM has roughly half the bandwidth, and the scanout arithmetic in ARCHITECTURE.md §5 does not close at half. |
| **16 MB flash** | A/B OTA halves it. Two 5 MB app slots and 5.8 MB of assets is what `partitions.csv` assumes. 8 MB means giving up either updates or assets. |
| **480×480 round panel with a documented init sequence** | The face is the product. The init sequence is the thing vendors most often leave out, and reverse-engineering one is days of work with no way to know you are finished. |

Add a microphone and a speaker and it does everything in this repository. Without
them, everything except voice still works — that is [ADR 0008](adr/0008-offline-first.md).

## The shortlist

**Waveshare ESP32-S3-Touch-LCD-1.85 / 2.1 (round).** 360×360 or 480×480, capacitive
touch, 16 MB flash, 8 MB PSRAM on the right variants. The reason to pick one is
that Espressif's own `esp-bsp` repository carries a board support package for
several of them, which means the panel init sequence is published, tested, and
someone else's problem. Check the exact SKU: the same product name covers
variants with 4 MB of flash and no PSRAM.

**Espressif ESP32-S3-LCD-EV-Board.** A development kit rather than a product: the
panel is on a ribbon, the enclosure is your problem, and it costs more. Worth it
if the panel is not settled, because it takes several different ones.

**A bare ESP32-S3-WROOM-1-N16R8 plus a panel you choose.** The most flexible and
the most work. Take it only if a specific panel matters to you, and only with its
datasheet in hand — not a product page claiming "compatible with Arduino".

## What to check before ordering

1. **The module suffix.** `N16R8` means 16 MB flash and 8 MB PSRAM. `N8R2`
   does not. This is the single most common way to buy the wrong board.
2. **Octal, not quad, PSRAM.** Usually written as OPI or "octal SPI".
3. **A published init sequence or an esp-bsp entry** for the exact panel.
4. **The touch controller part number** — FT5x06, GT911 and CST816 are all
   common and all different. Any of them is fine; not knowing which is not.
5. **A microphone**, if voice matters. An I2S MEMS microphone is the easy case;
   an analogue one means an ADC and a noise problem.

## Once it arrives

Add a directory under `boards/` with its pin map, build, and flash —
[flashing.md](flashing.md) has the steps and the failure table. The drivers
behind `nev_board` are the only code that should need to change, and if
something else does, that is a layering bug worth reporting rather than working
around.
