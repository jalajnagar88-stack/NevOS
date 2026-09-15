# The Persona

> Status: **implemented at M2.** `components/nev_persona`.

The face is the product. This page is how it works and how to drive it.

---

## 1. A mood is a row of numbers

There are no animation clips in NEVOS. A mood is a target set of **17 scalars**
— eye openness and slant, gaze, brow, mouth, head, accents — and a transition is
interpolation between two such sets.

Three things follow, and they are the reason for the design:

- Transitions are tweened for free. There is nothing to author.
- A mood change **mid-transition** cannot glitch: the new tween starts from
  wherever the face currently is, not from the previous mood's preset.
- Adding a mood costs one row of a table, not a new animation.

The table lives in `src/face_presets.c`, written positionally with a column
header, because tuning a face means comparing rows and a base-plus-override
form hides exactly the differences you need to see.

## 2. The split, and why

```
face_params.h    17 scalars, 8 presets, the lerp        no LVGL
persona_core.h   mood machine, tween, blink, idle       no LVGL, no bus, no clock
face.h           renderers                              LVGL
persona.h        binds core to bus and renderer         LVGL + bus
```

`persona_core` takes `now_ms` as an argument rather than reading a clock, and
seeds its randomness explicitly. A personality that depends on wall-clock time
and on an RNG is untestable by accident; this one runs deterministically, so
"does it blink at a natural cadence", "is the sleepy face actually still" and
"does the held mood expire correctly across a 32-bit millisecond wrap" are all
assertions rather than things you squint at.

## 3. Driving it

```c
nev_persona_set_mood(NEV_MOOD_HAPPY, 255, 2200);
```

- **intensity** (0–255) scales the mood away from idle. 128 is halfway. One
  event type can therefore produce a nudge and another a full reaction without
  needing two moods.
- **hold_ms** of 0 makes it the new *resting* state. Non-zero returns to the
  previous resting state afterwards — which is what makes a tap a reaction
  rather than a mode change.

Callable from L4 and L5 (downward, legal). **Not** callable from another L3
module: `nev_bridge` publishes `BRIDGE.MOOD_HINT` instead.

## 4. What the world makes it feel

The mapping table in `src/persona.c` is the persona's half of the product;
everything else there is plumbing.

| Event | Mood | Intensity | Hold |
|---|---|---|---|
| `INPUT.GESTURE_SHAKE` | curious | 255 | 1.6 s |
| `INPUT.GESTURE_TAP` | happy | scales with tap strength | 2.2 s |
| `POWER.IDLE_ENTER` / `IDLE_EXIT` | sleepy / idle | 255 | resting |
| `POWER.BATTERY` below 15%, unplugged | concerned | 255 | 5 s |
| `GAME.HIGHSCORE_BEAT` | celebrating | 255 | 3.2 s |
| `GAME.OVER` | curious | 170 | 1.8 s |
| `BRIDGE.AGENT_TOKEN` / `AGENT_DONE` | thinking / idle | 255 | resting |
| `BRIDGE.MOOD_HINT` | from the payload | from the payload | from the payload |

Two of those are deliberate judgements rather than obvious mappings. Losing an
arcade game is **curious**, not concerned — a two-minute game is not a crisis,
and a device that looks worried every time you lose gets tiring. A low battery
while charging is **not** a worry, so concern is gated on being unplugged.

## 5. Idle behaviour

The device must never look frozen, and must never pull focus from the work on
the desk beside it.

- **Blink** at a randomised 2.2–6.2 s interval; 70 ms shut, 110 ms open.
  Multiplicative on eye openness, so blinking during a squint narrows what is
  already narrow rather than overriding the expression.
- **Gaze drift** to a new resting point every 3–7 s, approached exponentially so
  the eyes glide rather than step.
- **Head bob** on a slow 5.5 s cycle, amplitude 0.035.

Each mood declares how much of this it tolerates. **Sleepy is completely still**
— a sleeping face that keeps glancing around is not asleep — and focused is
nearly so, because concentration reads as an absence of motion.

## 6. Adding a mood

1. Add it to `nev_mood_t` and to `kNames` in `face_presets.c`.
2. Add a row to the preset table and an entry to `kEnterMs` and `kLiveliness`
   in `persona_core.c`.
3. Point an event at it in the table in `persona.c`, if anything should cause it.

No renderer change is needed: the renderer interprets parameters, not moods.
