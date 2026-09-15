# App lifecycle

> How the shell decides what is running, what is kept warm, and what gets closed.

## States

```
        nev_shell_launch(id)
   ┌──────────────────────────────▶ RUNNING ──────────┐
   │                                  │               │
 (none)                        on_suspend      on_close
   │                                  ▼               │
   │                              SUSPENDED ──────────┤
   │                                  │               │
   └──────── on_resume ◀──────────────┘               ▼
                                                   (closed)
```

- **RUNNING** — the foreground app. It alone receives `on_event` and `on_tick`.
- **SUSPENDED** — its LVGL objects still exist but are hidden. Resuming shows
  them again; nothing is rebuilt. This is the whole point of keeping it around.
- **CLOSED** — `on_close` has run and the root container is deleted.

## One foreground, a few warm

`NEV_SHELL_MAX_LIVE` is **3**: one foreground plus two suspended. Beyond that
the least recently used suspended app is closed.

Three is small on purpose. On a device with 512 KB of internal memory, "keep
everything warm" is how you run out, and an app that gets closed is an app that
rebuilds in a few milliseconds — whereas an app that causes an allocation
failure somewhere else takes down whatever was unlucky.

The foreground app is never evicted. The user is looking at it.

## Memory pressure

`SYS.MEM_PRESSURE` on the bus makes the shell close its least recently used
background app immediately, without waiting for the next launch. `memory_budget_kb`
in the descriptor is declared, not measured — an app that understates it will
be the one still alive when something else fails.

## Navigation

| Gesture | Effect |
|---|---|
| Tap a home tile | launch or resume that app |
| Swipe right | back: dismiss a modal, else go home |
| Button B | the same |

Back dismisses an open modal *before* it leaves the app, so a confirmation
dialog can be escaped without losing your place.

Button B is back everywhere, in every app. One consistent escape hatch matters
more than letting each app decide what B means.

## Resuming where you were

The shell persists the foreground app id in `NEV_SET_LAST_APP` and relaunches
it on boot. A device that always wakes to the home grid makes you re-navigate
every time it sleeps.

If that app fails to launch — it was removed in an update, say — the shell
clears the setting and shows the home grid rather than failing repeatedly.

## What the shell owns, and what it does not

The shell owns the screen, the status bar, and the root container of every app.
Apps own everything inside their own root and nothing outside it.

That division is what makes the status bar reliable: no app can accidentally
cover it, restyle it, or leave it showing stale state, because no app can reach
it.
