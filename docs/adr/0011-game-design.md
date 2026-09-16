# ADR 0011 — Game rules live apart from game rendering

**Status:** accepted (M4)

## Context
Five original arcade games, each needing to be genuinely playable rather than a
demo. The question is how to know they are.

## Decision
Every game splits into `<game>_core.c` — the rules, with no LVGL, no clock of
its own and randomness injected as a callback — and `<game>_app.c`, the
renderer and the difficulty tuning. The same split as `persona_core` and
`nev_store`.

## Consequences
The reason is not tidiness. A headless auto-player cannot establish that a game
is correct: on Snake's 400-cell grid a random walk essentially never lands on
the one food cell, so a 900-frame run "passes" while never eating and never
dying. That happened, twice, before the split.

What the split buys is assertions on the things that actually decide whether a
game feels fair:

- entering the cell your tail is vacating is legal, but entering it while you
  are growing is fatal — the pair that makes Snake feel right or broken
- a fumbled swipe is refused, not fatal, and two fast turns are both applied in
  order rather than combining into a 180 into your own body
- Breakout's ball speed is conserved exactly over a four-thousand-step rally,
  so a tuned difficulty curve does not quietly drift into impossible
- every Runner gap stays wider than a jump at every speed, so the generator
  gets harder by leaving less margin and never by becoming unclearable
- Match never opens with a free match, never accepts a pointless swap, and
  never resolves into a dead board
- Reflex voids a tap faster than human reaction, so the high-score table means
  something

Each of those is a bug that a playtest finds late and a test finds immediately.
The turn-queue bug was caught by a test written before it had ever been seen.

The cost is an indirection per game and a callback for randomness. Both are
small, and the randomness injection has a second benefit: a failing board or a
bad spawn is reproducible from a seed.

## Two design decisions recorded with it

**Paddle control is a direct drag, not tilt.** Tilt is more novel and shows off
the IMU, but it cannot be built or tuned until M5 and cannot be tested in the
simulator at all — a control surface shipped untuned. Drag works identically on
both targets. Tilt joins it at M5 as an alternative, not a replacement.

**Difficulty is approachable then steep.** The first thirty seconds of each
game are gentle enough that anyone picks it up, and the curve steepens sharply
after. This is a desk toy played in short bursts between work: a bad run still
has to last long enough to feel like a go, or the device gets closed and not
reopened. Arcade-authentic punishment would be more faithful to the genre and
worse for the product.
