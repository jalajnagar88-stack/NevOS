# ADR 0002 — Per-subscriber queues, no central dispatcher

**Status:** accepted (M1)

## Context
Every subsystem communicates over the bus. The fan-out mechanism determines
latency, ordering, and where backpressure accumulates.

## Options
1. **Per-subscriber rings** — publish memcpys into each matching subscriber.
2. **Central queue + dispatcher task** — one global queue, fanned out.
3. **Hybrid** — direct for INPUT/DISPLAY, dispatched for the rest.

## Decision
Option 1.

## Consequences
Lowest latency: an input event reaches the render task on its next scheduling
slot, not the one after a dispatcher's. Per-subscriber depth and overflow policy
become tunable, and there is no single point of backpressure.

The price, stated plainly because handlers must be written for it: **there is no
global ordering across publishers.** FIFO holds per publisher–subscriber pair
only. No protocol may be encoded as "A from X arrives before B from Y"; pairing
and OTA sequencing therefore live as state machines in one module each,
validating against their own state.

Option 2 would have bought total ordering and one trace point, at a thread hop
on every event. Option 3 was rejected for having two sets of semantics to
document and reason about.
