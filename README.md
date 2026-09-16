# NEVOS

An embedded operating system for a desktop companion robot: an ESP32-S3 with a
480x480 touch panel, a face with a personality, five original arcade games,
focus and capture tools, and a local-first agent daemon running on your PC.

**Status: M6.** Kernel, persona, shell, five original games, the companion
daemon, and the link between them. The simulator discovers a real daemon on the
network, pairs with it by showing a code, and holds a conversation — using the
same code the device will run. Hardware bring-up (M5) is next. See
[ARCHITECTURE.md](ARCHITECTURE.md).

```bash
./tools/build.sh sim                  # the shell
./tools/build.sh sim -- --app snake   # straight into a game
./tools/build.sh sim -- --persona     # the face
```

---

## Quick start

```bash
git clone --recursive <this repo> && cd nevos

# macOS
brew install cmake ninja sdl2
# Debian/Ubuntu
sudo apt-get install -y cmake ninja-build libsdl2-dev clang-format

./tools/build.sh sim        # opens a 480x480 window
./tools/build.sh test       # host unit tests, no hardware
./tools/build.sh check      # what CI runs
```

The companion daemon, which does the talking and the listening, runs on your own
computer and needs nothing installed to try:

```bash
cd companion && cargo run -p nevosd -- --mock
```

Then open <http://127.0.0.1:4822/> for the control panel: what is running where,
your notes, and a one-click purge. No account, no API key, no cloud service —
see [docs/daemon.md](docs/daemon.md).

With the daemon running, the simulator can talk to it:

```bash
./tools/build.sh sim -- --bridge          # finds it, shows a pairing code
./tools/build.sh bridge-live              # the same thing, as a test
```

No ESP-IDF install is needed for any of the above. The simulator and the test
suite build with cmake, ninja and SDL2 alone — that is the point of the
`nev_port` layer.

In the window: the mouse drives touch, `A` and `B` drive the two physical
buttons, `Esc` quits.

### Headless

For CI, containers, and any machine without a display:

```bash
./tools/build.sh headless 120     # renders 120 frames, writes build/shot.png
```

Same binary logic, same render path, no window. A run that draws no pixels
fails, so this catches a broken flush path that a compile-only check would not.

### Device firmware

```bash
# one-time
mkdir -p ~/esp && cd ~/esp
git clone -b v5.2.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3

. $HOME/esp/esp-idf/export.sh
./tools/build.sh device build
./tools/build.sh device flash monitor
```

At M1 the device target runs `nev_port` and `nev_kernel` on real silicon. The
display, touch, audio and radio stacks arrive at M5, when the pins in
`boards/devkit_480/board_config.h` can be filled in from an actual schematic.

---

## Layout

| Path | |
|---|---|
| `components/nev_port/` | L-1 — tasks, sync, time, memory, logging; one impl per target |
| `components/nev_board/` | L0 — the only place that knows about hardware |
| `components/nev_kernel/` | L1 — the event bus, blob pool, storage, logging |
| `components/nev_services/` | L2 — display, audio, input, net, OTA, power |
| `components/nev_persona/` | L3 — the face and its moods |
| `components/nev_appkit/` | L4 — app registry, lifecycle, navigation, UI kit |
| `apps/` | L5 — one self-contained folder per app |
| `components/nev_bridge/` | L6 — CBOR over WebSocket to the daemon |
| `boards/<name>/board_config.h` | every pin number in the project, in one file |
| `targets/{host,tests,esp32s3}/` | the three build roots |
| `companion/` | the Rust daemon (M6) |

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — layering, the three invariants, task
  topology, and the rendering and memory budgets
- [docs/event-bus.md](docs/event-bus.md) — the contract every subsystem is
  written against. Read this before adding a subscriber.
- [BUDGET.md](BUDGET.md) — measured flash, PSRAM, SRAM and frame time
- [docs/adr/](docs/adr/) — decisions and what they cost

## The rules

Three, and CI enforces them (`tools/ci/lint_layers.py`):

1. **Downward calls only.** From L3 up, no sideways calls either — subsystems
   exchange events, not function calls.
2. **Hardware is a detail.** Nothing above L0 includes a vendor header or names
   a GPIO.
3. **Nothing blocks the frame.** One render task, a 33 ms budget, no blocking
   calls in app code.

## Originality

Every game in NEVOS is an original work inspired by a genre. There is no
emulation, no ROM loading, and no trademarked character, level layout, sprite
or music anywhere in this repository.
