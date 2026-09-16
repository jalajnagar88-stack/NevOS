# ADR 0012 — A generated, dependency-free codec checked three ways

**Status:** accepted (M6)

## Context
The device speaks C and the daemon speaks Rust. Hand-written structs on both
sides of a wire protocol drift, and the drift shows up as a corrupt field in
production rather than as a compile error.

## Decision
One schema (`schema/nevos.toml`) generates the C codec, the Rust codec, and a
set of golden vectors. The Rust crate takes **no dependencies**: its CBOR writer
and reader are hand-written to mirror the C ones.

## Consequences
The obvious alternative was a CBOR crate on the Rust side. Rejected because it
reintroduces the problem in a subtler form: a crate has its own opinion about
canonical encoding, and a disagreement about the shortest-form rule produces
different bytes for the same message. That is not caught by a round-trip test
on either side alone — only by comparing bytes across implementations.

So the golden vectors are encoded by a **third** implementation, the canonical
encoder inside `gen.py`. C and Rust are each checked against it, byte for byte.
Any two of the three disagreeing fails a build.

The cost is writing CBOR three times. It is a small, bounded subset — integers,
strings, arrays, booleans, one float type — and writing it deliberately is what
makes the subset small. Every construct the decoders do not implement is one
that cannot be used against a device with 512 KB of RAM parsing bytes from the
network.

Two practical consequences came out of building it, both about generated files
and formatters:

- `cargo fmt` reformats the generated Rust, which then reports as stale. rustfmt
  follows `mod` declarations, so formatting `lib.rs` alone recurses into it, and
  the `--skip-children` flag that would prevent that is nightly-only. Rust
  formatting is therefore not gated in CI.
- `clang-format` had the same effect on the generated C. Formatting is now
  skipped for any file carrying the generated banner — pinning it would also
  mean pinning a clang-format version across Linux and macOS.

Generated code does not need to be pretty. It needs to be identical.
