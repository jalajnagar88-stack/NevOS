# ADR 0006 — Two build roots under `targets/`

**Status:** accepted (M1)

## Context
ESP-IDF requires the project's top-level `CMakeLists.txt` to include
`project.cmake` before `project()`. A plain host build wants a different root.

## Options
1. **One root that dispatches** on a `NEVOS_TARGET` variable.
2. **Separate roots**: `targets/esp32s3`, `targets/host`, `targets/tests`.

## Decision
Option 2.

## Consequences
Each build system gets the root it expects and neither is bent around the other.
The file lists stay shared through `components/*/sources.cmake`, which both the
IDF component manifests and the host CMake read — one source of truth, the same
anti-drift reasoning as the M6 protocol codegen. Source lists are explicit
rather than globbed so that adding a file is a visible change in review.

The cost is three `CMakeLists.txt` files instead of one, and commands that carry
a `-C targets/...`. `tools/build.sh` hides that for everyday use.

Option 1 was rejected as clever and fragile: the ordering constraints around
`project.cmake` make a conditional root easy to break and hard to debug.
