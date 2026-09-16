# ADR 0015 — A charging device dims but never goes dark

**Status:** accepted (M7)

## Context

The device needs an idle policy: when to dim, when to turn the screen off, and
what to do about the touch that wakes it. The obvious answer is the phone
answer — a single timeout, screen off, any touch wakes and is delivered — and
it is wrong here in three specific ways.

## Decision

**Two stages, not one.** The screen dims at two thirds of the sleep timeout and
goes dark at the timeout. One number to configure, two stages of behaviour; the
dim is a warning that the screen is about to go, and a device that goes from
full brightness to black feels broken rather than asleep. A dim never reaches
zero brightness, because a dim you cannot read is a slower way of turning the
screen off.

**On a charger, it stops at dim.** This is a thing on a desk with a clock and a
face on it, and the reason to leave it plugged in is to be able to glance at it.
Turning the screen off to save power that is arriving down a cable saves the
wrong thing. On battery it goes dark, and only then may the CPU idle between
frames — a plugged-in device has nothing to save and everything to lose by being
slow to wake.

**The waking touch is swallowed.** Reaching for a dark object, you cannot aim at
a button you cannot see, so the touch that wakes the screen must not also press
what was underneath it. A touch while merely dimmed *is* delivered: the screen
was readable, so the user meant what they hit.

**Only going dark publishes `POWER.IDLE_ENTER`.** Dimming is an absence of
input, not an absence of a person. The face going to sleep every time someone
paused to read the screen would be exhausting.

## Consequences

The policy lives in `power_core.c` with no board, no bus and no clock of its
own, so four minutes of idling runs in a microsecond and the interesting cases
are tests: a charger unplugged while the screen is already dark, a low battery
that halves the timeout, a touch landing in the same millisecond as a timeout,
and the 32-bit millisecond counter wrapping after 49 days.

The swallow had to be implemented twice. Gating the published event was not
enough: LVGL reads the touch controller directly in its own read callback, so
the waking tap still pressed what was under it — the first thing the simulator
did on waking was launch a game. Anything that gates input has to gate both
paths, and that is now stated where the read callback is.

The simulator grew a `--battery` flag, because a device permanently on mains
cannot reach the interesting half of this policy, and a fake discharge curve
would put those cases on a timer nobody wants to wait for.
