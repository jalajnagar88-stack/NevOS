# ADR 0007 — Schema-generated bridge codec

**Status:** accepted in principle (M1), implemented at M6

## Context
The device speaks C and the daemon speaks Rust. Hand-written structs on both
sides of a wire protocol drift, and the drift shows up as a corrupt field in
production rather than as a compile error.

## Decision
One schema file, `schema/nevos.proto.yaml`, generates both the C structs and
codec and the Rust types. Neither side is hand-written, so neither can drift.
Round-trip and fuzz tests run in CI in both languages against shared golden
vectors.

## Consequences
A code generator to maintain, and a build step. In exchange, a field added on
one side that is not handled on the other is a build failure, which is the only
reliable place to catch it. Golden vectors also mean an old device and a new
daemon can be tested against each other rather than assumed compatible.
