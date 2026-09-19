# Putting NEVOS on a device

> **Nothing in this file has been run on hardware yet.** No board has been
> chosen, so the steps are written from the ESP-IDF documentation and the
> project's own configuration, not from experience. Expect to correct it the
> first time — and please do correct it, rather than keeping the fix in your
> shell history.

Everything else in NEVOS is verified on the simulator, which is the point of
the `nev_port` layer. This page is the one part that cannot be.

## What you need

- **ESP-IDF v5.2 or newer.** Follow Espressif's install guide, then in every
  shell you build from:

  ```sh
  . $HOME/esp/esp-idf/export.sh
  ```

- **An ESP32-S3 with 8 MB of octal PSRAM and 16 MB of flash**
  (`ESP32-S3-WROOM-1-N16R8` or equivalent). Both numbers matter:
  `sdkconfig.defaults` configures octal PSRAM, and the partition table assumes
  16 MB. A quad-PSRAM module will boot and then fail to allocate the
  framebuffer — see [BUDGET.md](../BUDGET.md).

- **A 480×480 panel and a touch controller** the board layer knows about. There
  is one board definition, `boards/devkit_480/`, and it is where the pin numbers
  live. Adding a board means adding a directory there, not editing drivers.

## Build and flash

```sh
./tools/build.sh device build
./tools/build.sh device -p /dev/ttyUSB0 flash monitor
```

`device` passes everything after it to `idf.py`, so any idf command works:
`./tools/build.sh device menuconfig`, `... size-components`, `... erase-flash`.

On macOS the port is usually `/dev/cu.usbmodem*`; on Linux you may need to be in
the `dialout` group or the device will appear and refuse to open.

### Release builds

```sh
./tools/build.sh release build
```

The overlay in `targets/esp32s3/sdkconfig.release` turns on rollback, the task
watchdog and a quieter log, and stops assertions carrying filename strings.
**Flash a debug build first.** The point of rollback is to survive a bad update,
and a first flash is not an update — if the release build does not boot, you
want to be looking at a debug log rather than at a device that has quietly
reverted to nothing.

## First boot

The device has no keyboard, so setup happens in this order:

1. It shows its eyes opening, then the home screen. **Everything on it works at
   this point** — clock, games, timer, the face. No network, no computer, no
   account ([ADR 0008](adr/0008-offline-first.md)).
2. Wi-Fi: **Settings → Wi-Fi**. Type the network name and the password on the
   on-screen keyboard and press Save. The password is never shown again, not
   even as dots, because a device on a desk is looked at by whoever walks past
   it. (The radio behind this arrives with the board; the screen, the state
   machine and the stored credentials work on the simulator today.)
3. Start `nevosd` on your computer — see [daemon.md](daemon.md).
4. Open **Computer** on the device. It finds the daemon and shows six digits.
5. Type those digits into the control panel at <http://127.0.0.1:4822/>.

That is the whole setup. The code is the only step that needs a human, and it
is there so that a device on the network cannot pair itself
([ADR 0014](adr/0014-two-listeners.md)).

Then hold **button A** — or the button on the screen in Notes and Ask — and
talk. A press has to survive 120 ms before the microphone opens, so a sleeve
brushing it records nothing, and a capture shorter than 350 ms is thrown away
rather than sent; both are deliberate, and `talk_core.h` says why.

## When something is wrong

| What you see | What it usually is |
|---|---|
| Boot loop with a PSRAM error | quad-PSRAM module, or `CONFIG_SPIRAM_MODE_OCT` off |
| Blank screen, serial log fine | panel init sequence or backlight pin in `boards/*/board_config.h` |
| Touch reads inverted | axis swap or mirror in the board's touch configuration |
| Device never finds the daemon | the daemon's mDNS is blocked, or the two are on different subnets — many home routers isolate guest networks |
| Pairing code refused | the attempt expires after three minutes; five wrong codes lock the endpoint for five |
| Frames over budget in the log | see [BUDGET.md](../BUDGET.md) §5; the first thing to try is fewer full-screen redraws |

A panic is written to the coredump partition and survives a reboot:

```sh
./tools/build.sh device coredump-info
```

## Factory reset

Settings → Factory reset erases Wi-Fi, the pairing token and every setting, and
stops the microphone and the link immediately rather than at the next restart —
because a factory reset is what someone does before giving the device away.

High scores go too. Nothing on the companion computer is touched: the device
cannot reach into someone's notes, and a reset device is not a reason to delete
them. Use the control panel's purge for that.
