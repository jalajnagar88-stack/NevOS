# ADR 0009 — Push-to-talk through M6; no on-device wake word

**Status:** accepted (M1) · **Revisit:** post-M7

## Context
The brief allows "push-to-talk or wake word" for the `agent` app. On-device wake
word means Espressif's esp-sr WakeNet — roughly 500 KB to 1 MB of a 5 MB app
slot, ESP32-only, and untestable in the simulator.

## Decision
Push-to-talk on button A through M6. Wake word is a post-M7 addition behind a
capability flag, if still wanted.

## Consequences
Identical behavior in the simulator and on hardware, no flash cost, nothing to
train, and M6 stays focused on the daemon — which is where the differentiation
actually is.

A wake word that only exists on hardware would violate the rule that hardware is
a detail, and would be the one feature no simulator run could cover. The
rejected third option, streaming audio continuously for the PC to detect on,
sits badly against the local-first posture and the recording-indicator
invariant: a mic that is always on and always streaming is exactly what the
privacy design exists to avoid.
