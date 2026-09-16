#!/usr/bin/env bash
# NEVOS build driver. Everything a contributor needs, in one place.
#
#   ./tools/build.sh sim         window (SDL2), then run it
#   ./tools/build.sh headless    no window; renders frames and captures a PNG
#   ./tools/build.sh test        host unit tests
#   ./tools/build.sh proto       regenerate the codec from schema/nevos.toml
#   ./tools/build.sh rust        companion daemon tests
#   ./tools/build.sh lint        layering rules, and that the codec is current
#   ./tools/build.sh format      apply clang-format
#   ./tools/build.sh asan        every screen under AddressSanitizer
#   ./tools/build.sh check       lint + format + test + asan + both host builds
#   ./tools/build.sh device      ESP32-S3 firmware (needs ESP-IDF on PATH)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GEN=$(command -v ninja >/dev/null 2>&1 && echo "-GNinja" || echo "")
BUILD() { cmake --build "$1" "${@:2}"; }

need_submodules() {
  if [ ! -f third_party/lvgl/lvgl.h ] || [ ! -f third_party/unity/src/unity.c ]; then
    echo "==> fetching submodules"
    git submodule update --init --recursive
  fi
}

case "${1:-check}" in
  sim)
    need_submodules
    cmake -S targets/host -B build/host $GEN -DNEVOS_DISPLAY=sdl2 -DCMAKE_BUILD_TYPE=Debug
    BUILD build/host
    shift || true
    exec ./build/host/nevos_sim "$@"
    ;;

  headless)
    need_submodules
    FRAMES="${2:-120}"
    OUT="${3:-build/shot}"
    cmake -S targets/host -B build/host-headless $GEN -DNEVOS_DISPLAY=headless -DCMAKE_BUILD_TYPE=Debug
    BUILD build/host-headless
    ./build/host-headless/nevos_sim --frames "$FRAMES" --shot "${OUT}.ppm"
    python3 tools/ppm2png.py "${OUT}.ppm" "${OUT}.png"
    ;;

  test)
    need_submodules
    cmake -S targets/tests -B build/tests $GEN -DCMAKE_BUILD_TYPE=Debug
    BUILD build/tests
    ctest --test-dir build/tests --output-on-failure
    ;;

  asan)
    # Runs every screen under AddressSanitizer. This is not optional polish:
    # it is what caught a NULL label dereference that only appeared thirty
    # frames in, because the event that triggered it is published once a second.
    need_submodules
    cmake -S targets/host -B build/asan $GEN -DNEVOS_DISPLAY=headless \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    BUILD build/asan
    S=$(mktemp)
    for mode in "" "--app settings" "--app clock" "--persona --script" "--boot"; do
      echo "==> asan: nevos_sim ${mode:-<shell>}"
      rm -f "$S"
      # shellcheck disable=SC2086
      ./build/asan/nevos_sim --settings "$S" $mode --frames 120
    done
    rm -f "$S"
    echo "asan: clean"
    ;;

  proto)
    # Regenerates the C and Rust codecs and the golden vectors from the schema.
    python3 tools/schema/gen.py
    ;;

  rust)
    if ! command -v cargo >/dev/null 2>&1; then
      echo "cargo not found; install Rust to build the companion daemon" >&2
      exit 1
    fi
    (cd companion && cargo test "${@:2}")
    ;;

  bridge-live)
    # The bridge against a real daemon. Start one first:
    #   (cd companion && cargo run -p nevosd -- --mock)
    "$0" test >/dev/null
    ./build/tests/bridge_live
    ;;

  lint)
    python3 tools/ci/lint_layers.py
    # A checked-in codec that no longer matches the schema is the exact drift
    # the generator exists to prevent, so a stale one fails the build.
    python3 tools/schema/gen.py --check
    ;;

  format | format-check)
    # Generated sources are excluded. Formatting them would make
    # `gen.py --check` report them as stale, and pinning their formatting to
    # whatever clang-format version happens to be installed would make that
    # check fail differently on Linux and macOS. Generated code does not need to
    # be pretty, it needs to be identical.
    SOURCES=$(find components apps targets boards \( -name '*.c' -o -name '*.h' \) 2>/dev/null \
      | xargs -r grep -L "GENERATED FROM schema/nevos.toml")
    if [ -z "$SOURCES" ]; then echo "no sources found"; exit 0; fi
    if [ "$1" = "format" ]; then
      echo "$SOURCES" | xargs clang-format -i
      echo "formatted"
    else
      echo "$SOURCES" | xargs clang-format --dry-run --Werror
      echo "format ok"
    fi
    ;;

  device)
    if ! command -v idf.py >/dev/null 2>&1; then
      echo "ESP-IDF not on PATH. Run: . \$HOME/esp/esp-idf/export.sh" >&2
      exit 1
    fi
    need_submodules
    idf.py -C targets/esp32s3 "${@:2}"
    ;;

  check)
    "$0" lint
    "$0" format-check
    "$0" test
    "$0" headless 60 build/shot
    "$0" asan
    if command -v cargo >/dev/null 2>&1; then "$0" rust; else echo "(skipping rust: no cargo)"; fi
    cmake -S targets/host -B build/host $GEN -DNEVOS_DISPLAY=sdl2 -DCMAKE_BUILD_TYPE=Debug >/dev/null
    BUILD build/host
    echo
    echo "all checks passed"
    ;;

  clean)
    rm -rf build
    echo "cleaned"
    ;;

  *)
    sed -n '2,12p' "$0"
    exit 2
    ;;
esac
