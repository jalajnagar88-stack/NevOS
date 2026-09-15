# The NEVOS Event Bus

> Status: **implemented at M1** in `components/nev_kernel`, with the suite in
> `components/nev_kernel/test/`. This is the contract every NEVOS subsystem is
> written against. If you are
> adding a service, a persona behavior, or an app, this is the page to read.

The bus is the only sanctioned way for NEVOS subsystems to communicate. Direct
function calls between L3+ modules are a layering violation and CI rejects
them. That restriction sounds severe until you notice what it buys: any
subsystem can be tested with a fake bus, any event stream can be recorded and
replayed, a new consumer can be added without editing the producer, and the
whole system's behavior is inspectable at one point.

---

## 1. Shape of the thing

```
  publisher task                                     subscriber task
  ──────────────                                     ───────────────
  nev_bus_publish(&ev) ──┐
                         │   for each subscriber whose mask matches:
                         ├──▶ [ ring 16 ]  nev_ui ────▶ nev_bus_recv(...)
                         ├──▶ [ ring  8 ]  nev_net ───▶ nev_bus_recv(...)
                         └──▶ [ ring  4 ]  nev_sys ───▶ nev_bus_recv(...)
                             memcpy, 32 bytes, no allocation, no thread hop
```

Publishing is a bounded memcpy into each matching subscriber's ring, under a
short critical section. There is no dispatcher task and no intermediate queue.
Cost scales with the number of *matching* subscribers, which is typically one
to three and is capped at 16 in total.

**What you get:** minimum latency (a high-priority input event reaches the UI
task on its next scheduling slot, not the one after a dispatcher's), per-
subscriber queue depth and overflow policy, and no single point of backpressure.

**What you give up:** there is no global total ordering across publishers. See
§6 — the ordering you actually get is enough, but you must know what it is.

---

## 2. The event

Exactly 32 bytes. Fixed size is what makes the rings allocation-free and the
memcpy a single stride.

```c
typedef struct {
    uint16_t type;      /* NEV_EVT_* — domain in high byte, id in low byte */
    uint8_t  flags;     /* NEV_EVF_*                                        */
    uint8_t  source;    /* NEV_SRC_* — publishing module, for tracing       */
    uint32_t seq;       /* monotonic, assigned by the bus                   */
    uint64_t ts_us;     /* nev_now_us() at publish                          */
    union {             /* 16 bytes                                         */
        uint8_t  raw[16];
        int32_t  i32[4];
        float    f32[4];
        struct { uint16_t handle; uint16_t reserved; uint32_t len; } blob;
        struct { int16_t x, y; uint8_t action, id; } touch;
        struct { uint8_t mood, intensity; uint16_t duration_ms; } mood;
        /* ... one struct per event family, all <= 16 bytes, asserted at compile time */
    } p;
} nev_event_t;

_Static_assert(sizeof(nev_event_t) == 32, "event must stay 32 bytes");
```

`seq` and `ts_us` are filled in by the bus, not by the publisher. Together they
make a captured event log replayable and make "did the UI task fall behind?"
answerable from a trace rather than from a guess.

---

## 3. Domains and types

The high byte of `type` is the domain. Subscriptions filter on a 32-bit domain
bitmask, so matching is a single `&` — no string topics, no list walking.

| Domain | Value | Published by | Examples |
|---|---|---|---|
| `SYS` | `0x01` | kernel | `BOOT_DONE`, `MEM_PRESSURE`, `BUS_OVERFLOW`, `TICK_1S` |
| `INPUT` | `0x02` | `input_service` | `TOUCH`, `BUTTON_DOWN/UP`, `GESTURE_SHAKE`, `GESTURE_TILT`, `GESTURE_TAP` |
| `DISPLAY` | `0x03` | `display_service` | `FRAME_BEGIN`, `FRAME_STATS`, `BACKLIGHT_CHANGED` |
| `AUDIO` | `0x04` | `audio_service` | `CAPTURE_START/STOP`, `CHUNK`, `VAD_BEGIN/END`, `PLAY_DONE` |
| `NET` | `0x05` | `net_service` | `WIFI_UP/DOWN`, `DAEMON_FOUND`, `DAEMON_LOST` |
| `PERSONA` | `0x06` | `nev_persona` | `MOOD_CHANGED`, `BLINK` |
| `APP` | `0x07` | `nev_appkit` | `LAUNCH`, `SUSPEND`, `RESUME`, `CLOSE`, `NAV_HOME` |
| `BRIDGE` | `0x08` | `nev_bridge` | `PAIRED`, `TRANSCRIPT_PARTIAL/FINAL`, `AGENT_TOKEN`, `AGENT_DONE`, `NOTIFICATION`, `MOOD_HINT` |
| `POWER` | `0x09` | `power_service` | `BATTERY`, `IDLE_ENTER/EXIT`, `CHARGING` |
| `STORAGE` | `0x0A` | `nev_store` | `SETTING_CHANGED`, `FS_READY` |
| `GAME` | `0x0B` | `game_engine` | `SCORE`, `GAME_OVER`, `HIGHSCORE_BEAT` |
| `OTA` | `0x0C` | `ota_service` | `AVAILABLE`, `PROGRESS`, `READY`, `FAILED` |

**A domain has exactly one producer.** `INPUT` events come from
`input_service` and from nowhere else; no app may synthesise a touch. This is
what makes "who published this?" a question with one answer, and it is checked
by the `source` field in debug builds.

All types live in one header, `nev_kernel/nev_events.h`, as an X-macro list
that generates the enum, the name strings for tracing, and the payload-size
static assertions from a single source.

---

## 4. Subscribing

```c
static nev_sub_t *s_sub;

void persona_init(void) {
    const nev_sub_cfg_t cfg = {
        .name        = "persona",
        .domains     = NEV_DOM(INPUT) | NEV_DOM(PERSONA) | NEV_DOM(POWER) | NEV_DOM(GAME),
        .depth       = 16,
        .full_policy = NEV_FULL_DROP_OLDEST,
        .coalesce    = true,
    };
    s_sub = nev_bus_subscribe(&cfg);
}
```

Subscriber slots, their rings, and their names are **statically allocated** —
`NEV_BUS_MAX_SUBS` (16) slots of `NEV_BUS_MAX_DEPTH` (32) events, sized at
compile time. `nev_bus_subscribe` hands out a slot; it does not allocate. It is
callable only during init, before the scheduler starts publishing, which
removes a whole class of race.

There is no unsubscribe. Subscribers are long-lived subsystems. Apps do not
subscribe directly — appkit owns one subscription and routes to the foreground
app's `on_event`, which means a crashing app cannot leak a bus slot.

---

## 5. Publishing

```c
nev_err_t nev_bus_publish(nev_event_t *ev);          /* seq and ts_us are filled in */
nev_err_t nev_bus_publish_type(uint16_t type, uint8_t source);  /* payload-free case */
```

**Publishing from an ISR is not yet supported**, and the earlier draft of this
document promised it before it was built. The bus lock is a recursive mutex, and
taking a mutex in an interrupt handler is not legal on FreeRTOS. Adding a
second, lock-free path purely for interrupts would mean two sets of ordering
semantics for no M1 benefit, since nothing publishes from an ISR yet.

When it is needed — the GT911 touch interrupt and the I2S DMA completion at M5 —
the shape is a small lock-free staging ring that `nev_bus_publish_isr` writes
with atomics and `nev_input`/`nev_audio` drain on their next wake, republishing
through the normal path. That keeps one set of semantics. Until then the
function does not exist rather than existing and being unsafe.

**`nev_bus_publish` never blocks and never fails the caller.** If a
subscriber's ring is full, the event is dropped *for that subscriber* per its
policy and the drop is counted. The return value reports whether any subscriber
dropped, so a publisher that cares can react, but ignoring it is safe and is
the normal case.

This is the single most important property of the bus. A publisher blocking on
a slow subscriber is how a 33 ms frame budget turns into a 300 ms stall. The
bus is explicitly lossy at the edges rather than implicitly stalling in the
middle.

### Overflow policy

| Policy | Behavior | Use for |
|---|---|---|
| `NEV_FULL_DROP_NEWEST` | discard the incoming event | audit-ish streams where history matters more than currency |
| `NEV_FULL_DROP_OLDEST` | evict the head, enqueue the new one | state updates, where the latest value is the useful one |
| `NEV_FULL_BLOCK` | **not offered** | — |

`NEV_FULL_BLOCK` does not exist. It is not an oversight.

### Coalescing

With `.coalesce = true`, if the newest event already queued has the same
`type`, the publish overwrites it in place instead of enqueuing. This matters
for genuinely high-rate streams: the IMU produces samples at 100 Hz while the
UI task wakes at 30 Hz, and the UI wants the current tilt, not a backlog of
stale ones. Coalescing is opt-in per subscriber, never per publisher — the
consumer is the one that knows whether it wants the latest or all of them.

### Overflow is visible

Each subscriber carries a drop counter. `nev_sys` samples them once a second
and publishes `SYS_BUS_OVERFLOW{sub_id, dropped}` (rate-limited to once per
second per subscriber) and logs a warning naming the subscriber. A queue that
is quietly overflowing in the field is a bug that takes weeks to find; a queue
that announces itself takes minutes.

---

## 6. Ordering — precisely

**Guaranteed:** for any single publishing task P and any single subscriber S,
events published by P are delivered to S in publish order. This is the
guarantee you actually build on, and it holds because each ring has one
in-pointer per critical section.

**Not guaranteed:** global order across different publishers. If `nev_input`
and `nev_net` publish at the same moment, two subscribers may observe them in
different relative orders.

**Therefore:** never encode a protocol as "event A from module X must arrive
before event B from module Y." Every event carries the state a handler needs to
act on it independently. Where sequencing genuinely matters — pairing, OTA —
the state machine lives in one module and its transitions are validated
against its own current state, so an out-of-order or duplicated event is
rejected rather than mis-sequenced.

`seq` and `ts_us` are available when a handler needs to reason about order
after the fact, which is rare and is a smell worth examining.

---

## 7. Large payloads: the blob pool

Sixteen bytes does not hold a 20 ms audio frame or a streamed sentence of agent
text. Copying those through the bus would blow the fixed-size property; heap
allocation on the hot path would blow the no-allocation property.

So: fixed-size buffers, preallocated at boot, handed around by handle with a
reference count.

```c
typedef uint16_t nev_blob_t;
#define NEV_BLOB_NONE ((nev_blob_t)0xFFFF)

nev_blob_t nev_blob_alloc(size_t len, uint8_t **out);  /* refcount = 1 */
uint8_t   *nev_blob_data(nev_blob_t h);
size_t     nev_blob_len(nev_blob_t h);
void       nev_blob_retain(nev_blob_t h);
void       nev_blob_release(nev_blob_t h);
```

| Class | Size | Count | Total | For |
|---|---|---|---|---|
| small | 512 B | 32 | 16 KB | transcript fragments, agent tokens, notifications |
| medium | 4 KB | 16 | 64 KB | audio frames, protocol messages |
| large | 64 KB | 4 | 256 KB | OTA chunks, whole-transcript buffers |

Roughly 336 KB, in PSRAM on device, plain `malloc` on host. Counts are
provisional and are tuned with measurements in `BUDGET.md`.

### The ownership protocol — get this right and nothing leaks

1. The **publisher** calls `nev_blob_alloc` (refcount becomes 1), fills the
   buffer, and publishes an event with `NEV_EVF_BLOB` set and the handle in
   `p.blob`.
2. **The bus retains once per successful enqueue**, inside the same critical
   section as the memcpy. Three subscribers accepting the event means refcount
   goes to 4.
3. The publisher calls `nev_blob_release` immediately after `nev_bus_publish`
   returns, giving up its own reference. Refcount is now exactly the number of
   subscribers that hold it.
4. Each **subscriber** calls `nev_blob_release` when it is finished with the
   event, always, on every path including error paths.
5. At zero, the buffer returns to its free list.

If *no* subscriber accepted the event, step 3 drops the count to zero and the
buffer is recycled immediately. If a subscriber's ring overflowed with
`DROP_OLDEST`, the bus releases the evicted event's blob as it evicts it. There
is no path that leaks a buffer and no path that frees one early.

**Blobs are immutable once published.** A subscriber that needs to modify the
data copies it. Multiple readers of one buffer is the whole point; a writer
would reintroduce the locking the bus exists to avoid.

Pool exhaustion returns `NEV_BLOB_NONE`, which the publisher must handle by
dropping the frame — never by blocking. Exhaustion publishes
`SYS_MEM_PRESSURE`. Debug builds tag each blob with the allocating module and
a timestamp, and `nev_sys` warns about any blob held for more than two seconds,
which is how a missing `release` gets found on the day it is written.

---

## 8. Receiving

```c
void nev_ui_task(void *arg) {
    nev_event_t ev;
    for (;;) {
        uint64_t frame_start = nev_now_us();

        /* drain, bounded: never let event handling eat the frame */
        int budget = 16;
        while (budget-- && nev_bus_recv(s_ui_sub, &ev, 0)) {
            persona_handle(&ev);
            appkit_handle(&ev);
            if (ev.flags & NEV_EVF_BLOB) nev_blob_release(ev.p.blob.handle);
        }

        lv_timer_handler();
        nev_frame_wait_until(frame_start + 33333);
    }
}
```

Two things this illustrates and both are contractual:

- **Draining is bounded.** A burst must not starve rendering. Leftover events
  wait one frame; the ring's depth and policy already say what happens if the
  backlog persists.
- **The receiver releases the blob**, exactly once, on every path. A `goto`
  that skips the release is the one bug this design is vulnerable to, and it is
  why debug builds warn on long-held blobs.

Tasks that are purely event-driven (`nev_net`, `nev_sys`) block in
`nev_bus_recv` with a timeout instead of polling.

---

## 9. Testing

The bus is host-testable with no RTOS, and so is everything written against it.

- **Bus unit tests**: publish/receive round trip, domain mask filtering, each
  overflow policy, coalescing, drop accounting, 32-byte size assertion,
  concurrent publish from N pthreads.
- **Blob tests**: refcount correctness across fan-out, release-on-evict,
  release-on-no-subscribers, exhaustion, and a leak assertion that the pool is
  fully free at teardown.
- **Subsystem tests**: a `nev_bus_test_harness` lets a test inject events and
  assert on what a subsystem published. The persona FSM's eight moods and every
  transition are tested this way, with no display and no hardware.
- **Record and replay**: `nev_bus_trace_start()` captures events into a ring;
  `tools/replay.py` feeds a captured trace back into the simulator. A bug
  reported from a device becomes a reproducible test case.

---

## 10. Adding an event — the whole procedure

1. Add the type to the X-macro in `nev_kernel/nev_events.h`, under the domain
   that owns it.
2. If it needs a payload, add a struct to the union and let the static assert
   check it fits in 16 bytes. If it does not fit, it is a blob.
3. Publish it from the domain's single producer.
4. Subscribe wherever it is needed. Do not modify the producer.

Step 4 is the point of all of this. Adding a consumer never touches the
producer, which is what keeps a system with a dozen subsystems from becoming a
graph where everything calls everything.

---

## 11. Deliberate omissions

Recorded so they are not re-litigated, and so a future contributor can see they
were considered:

- **No request/response.** The bus is one-way. Where a reply is needed, the
  request carries a correlation id and the reply is a separate event. Blocking
  round-trips on a 33 ms budget are not viable.
- **No priority events.** Task priority already handles urgency, and a priority
  queue inside the bus would break the FIFO guarantee in §6 for no real gain.
- **No dynamic unsubscribe.** Subsystems are permanent; apps route through
  appkit's single subscription.
- **No wildcard subscriptions.** A domain mask covering everything is
  expressible (`NEV_DOM_ALL`) and is intended for tracing only. A subsystem
  that wants every event has almost certainly been layered wrong.
