# ADR 0003 — Ref-counted blob pool for large payloads

**Status:** accepted (M1)

## Context
An event is 32 bytes with 16 bytes of payload. Audio frames, transcript
fragments and agent tokens do not fit. Copying them through the bus breaks the
fixed-size property; heap allocation on the hot path breaks the no-allocation
property.

## Options
1. **Fixed-size pool, handles with refcounts**, preallocated at boot.
2. **32-byte events only**, bulk data over a side channel per stream.
3. **Variable-size queue entries.**

## Decision
Option 1. Three classes: 32x512 B, 16x4 KB, 4x64 KB, ~336 KB of PSRAM allocated
once at init.

## Consequences
"Zero allocation on the hot path" stays literally true, and worst-case memory is
bounded by construction. Exhaustion returns `NEV_BLOB_NONE` and the caller drops
the frame; it never blocks.

The exposure is a missing `nev_blob_release` on some error path. Three things
address it: the ownership protocol is written out step by step in
docs/event-bus.md §7, `nev_blob_check_stale()` warns about any blob held over
two seconds, and every test asserts `nev_blob_all_free()` at teardown — which is
what caught the eviction-path leak while this was being written.

Option 2 was rejected because it reintroduces the direct cross-module coupling
the bus exists to remove. Option 3 makes queue-full accounting fuzzy and breaks
the fixed-stride memcpy.
