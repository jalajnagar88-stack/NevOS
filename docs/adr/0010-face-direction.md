# ADR 0010 — The face is two eyes and nothing else

**Status:** accepted (M2)

## Context
The persona is the product. Three directions were implemented behind one
renderer interface and rendered side by side across five moods, so the choice
could be made from output rather than from description.

## Options
1. **Vector eyes** — two rounded shapes, no mouth, no brows.
2. **Character face** — eyes, pupils, brows, mouth.
3. **Abstract orb** — one form; ring span, core size and offset carry everything.

## Decision
Option 1.

## Consequences
The strongest identity of the three and the cheapest to render: two shapes and
their lids, versus eleven primitives for the character face. A design that is
clearly not a face reads as more alive than a literal one, because the viewer
does the work — and it never lands in the uncanny valley, because it never
claims to be a face.

What it costs: with no mouth and no brows, every expression has to land in lid
geometry, slant and colour. Two consequences were found by building it.

**`mouth_curve` has to be reinterpreted.** In this design it drives which edge
the lids close from, and above a threshold it cross-fades the eye to an upward
arc. That arc is not decoration: subtracting a lid from a rounded rectangle can
only cut the bottom edge upward, leaving a band that is *thin in the middle* —
a moustache, not a squint. A happy eye is an arc, so it is drawn as one.

**`eye_slant` carries the entire weight of sadness and sternness**, so its range
here is wider than the other designs would have used. The character face's
mouth had been masking a preset bug: `happy` carried a negative `eye_slant`,
which is the *sad* direction, and with no mouth to argue otherwise the eyes-only
design rendered joy as misery. The presets are now correct for every design; a
mouth is a very effective way to hide a wrong eye.

Option 2 was the most legible today and remains the fallback if the eyes-only
vocabulary proves too narrow past M4. Option 3 confirmed the concern raised when
it was proposed: `concerned` and `sleepy` are genuinely hard to read without a
face. Abstraction buys timelessness and spends legibility, and legibility is not
what a device premised on reacting to you should be spending.
