#!/usr/bin/env python3
"""Enforce the NEVOS layering rules from ARCHITECTURE.md §9.

A rule that is not checked is a rule that is already broken somewhere. This
runs in CI and fails the build.

    tools/ci/lint_layers.py [--quiet]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ARCHITECTURE.md §2. A component may include from its own layer or below;
# from L3 up, peers may not include each other either (R1).
LAYER = {
    "nev_port": -1,
    "nev_board": 0,
    "nev_kernel": 1,
    "nev_services": 2,
    "nev_persona": 3,
    "nev_bridge": 3,
    "nev_appkit": 4,
}
PEER_ISOLATION_FROM = 3

# Only these paths may speak to the vendor SDK or name a GPIO.
PLATFORM_PATHS = (
    "components/nev_port/src/esp32s3/",
    "components/nev_board/src/esp32s3/",
    "targets/esp32s3/",
)
PIN_PATHS = ("boards/",)

VENDOR_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]((?:esp_|driver/|freertos/|hal/|soc/)[^">]*)[">]', re.M)
NEV_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](nev_\w+)/([^">]+)[">]', re.M)
GPIO_LITERAL = re.compile(r"\bGPIO_NUM_\d+\b")
BLOCKING_CALL = re.compile(r"\b(vTaskDelay|nev_sleep_ms|nev_sleep_until_us|usleep|sleep)\s*\(")
HEX_COLOR = re.compile(r"\b0x[0-9A-Fa-f]{6}\b")

SEARCH_DIRS = ["components", "apps", "targets", "boards"]
THEME_FILE = "components/nev_appkit/include/nev_appkit/theme.h"


def sources():
    for d in SEARCH_DIRS:
        base = ROOT / d
        if not base.exists():
            continue
        for p in sorted(base.rglob("*")):
            if p.suffix in (".c", ".h") and p.is_file():
                yield p


def rel(p):
    return p.relative_to(ROOT).as_posix()


def component_of(path):
    parts = Path(path).parts
    if parts[0] == "components" and len(parts) > 1:
        return parts[1]
    return None


def check(path, text, errors):
    r = rel(path)
    is_platform = any(r.startswith(x) for x in PLATFORM_PATHS)
    is_pin_file = any(r.startswith(x) for x in PIN_PATHS)
    comp = component_of(r)

    # R2 — hardware is a detail.
    if not is_platform:
        for m in VENDOR_INCLUDE.finditer(text):
            line = text[: m.start()].count("\n") + 1
            errors.append(f"{r}:{line}: includes vendor header '{m.group(1)}' outside L0/platform")

    if not is_pin_file:
        for m in GPIO_LITERAL.finditer(text):
            line = text[: m.start()].count("\n") + 1
            errors.append(f"{r}:{line}: GPIO literal '{m.group(0)}' outside boards/*/board_config.h")

    # R1 — downward calls only, and no sideways calls from L3 up.
    if comp in LAYER:
        mine = LAYER[comp]
        for m in NEV_INCLUDE.finditer(text):
            other = m.group(1)
            if other == comp or other not in LAYER:
                continue
            theirs = LAYER[other]
            line = text[: m.start()].count("\n") + 1
            if theirs > mine:
                errors.append(
                    f"{r}:{line}: {comp} (L{mine}) includes {other} (L{theirs}) — upward dependency"
                )
            elif theirs == mine and mine >= PEER_ISOLATION_FROM:
                errors.append(
                    f"{r}:{line}: {comp} includes peer {other} at L{mine} — use the event bus"
                )

    # R3 — nothing blocks the frame.
    if r.startswith("apps/"):
        for m in BLOCKING_CALL.finditer(text):
            line = text[: m.start()].count("\n") + 1
            errors.append(f"{r}:{line}: blocking call '{m.group(1)}' in an app — post a job instead")

    # §L4 — no app defines its own colors.
    if (ROOT / THEME_FILE).exists() and r.startswith("apps/") and r != THEME_FILE:
        for m in HEX_COLOR.finditer(text):
            line = text[: m.start()].count("\n") + 1
            errors.append(f"{r}:{line}: color literal '{m.group(0)}' — use a theme token")


def main():
    quiet = "--quiet" in sys.argv
    errors, scanned = [], 0
    for path in sources():
        scanned += 1
        check(path, path.read_text(encoding="utf-8", errors="replace"), errors)

    if errors:
        print(f"layer lint: {len(errors)} violation(s) in {scanned} files\n", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        print("\nSee ARCHITECTURE.md §9.", file=sys.stderr)
        return 1

    if not quiet:
        print(f"layer lint: {scanned} files, no violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
