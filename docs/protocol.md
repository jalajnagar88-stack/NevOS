# The bridge protocol

> Status: **codec implemented at M6.** Schema in
> [`schema/nevos.toml`](../schema/nevos.toml); transport and daemon follow.

The device and the daemon are written in different languages by different
people at different times. This page is about making it impossible for them to
disagree.

---

## 1. One schema, three implementations, one set of bytes

`schema/nevos.toml` is the only place a message is defined. From it,
`tools/schema/gen.py` produces:

| Output | Used by |
|---|---|
| `components/nev_bridge/include/nev_bridge/nev_proto.h` + `src/nev_proto.c` | the firmware |
| `companion/nevos-proto/src/generated.rs` | the daemon |
| `schema/golden/*.cbor` | both, as test fixtures |

Neither side is hand-written, so a field added to the schema that one side
forgets to handle is a **build failure**, not a corrupt value found in
production.

The golden vectors are encoded by a **third** implementation — the canonical
CBOR encoder inside `gen.py` itself. The C tests and the Rust tests both assert
they encode byte-for-byte to those vectors and decode back to the same values.
A disagreement between any two of the three fails a build rather than a session.

```bash
./tools/build.sh proto     # regenerate
./tools/build.sh lint      # fails if the checked-in output is stale
./tools/build.sh test      # C: golden vectors, bounds, fuzz
./tools/build.sh rust      # Rust: the same vectors
```

## 2. Wire format

```
frame   = u32 big-endian length | CBOR payload
payload = CBOR array: [ message_id, field0, field1, ... ]
```

Positional, not a map. It is roughly half the size, and the field names belong
in the generated code rather than on the wire.

Encoding is **canonical CBOR**: the shortest argument form for every value.
There is exactly one valid encoding of a given message, which is what makes
byte-for-byte golden comparison meaningful.

## 3. Compatibility, and the rules that keep it

The decoders enforce these, and the tests pin them:

- **Fields may only be appended.** Never reorder, never remove, never retype.
- **A decoder ignores trailing fields it does not know**, so a new daemon can
  talk to an old device.
- **A decoder leaves missing trailing fields at zero**, so an old daemon can
  talk to a new device.
- **Message ids are permanent.** A retired message keeps its id reserved.

Without the first two rules every schema addition is a flag day for every
deployed device.

## 4. What the parser refuses, and why

The decoder is the one part of NEVOS that parses bytes from the network, on a
device with 512 KB of RAM. The CBOR subset is small on purpose: **every
construct it does not implement is a construct that cannot be used against it.**

Refused outright:

| Construct | Why |
|---|---|
| Indefinite-length items | the standard way to make a decoder loop on attacker input |
| Maps, tags, 64-bit floats | unused, so not attack surface |
| Nested arrays | the protocol never nests; nesting is how you make a parser recurse |
| Reserved additional-info values (28–30) | not valid CBOR |

Checked before being trusted:

- **Every declared length** against the bytes actually present. A header
  claiming four gigabytes is the classic way to walk a parser off its buffer.
- **Every string** against its schema bound. Refused, never truncated: a
  silently shortened device name or pairing token is worse than a rejected
  message.
- **Every integer** against the width of the field it is read into.
- **The frame length prefix** against `max_frame_bytes`. It is the first thing
  a peer controls and the device cannot allocate its way out of a lie.

Byte fields decode to a **borrowed pointer** into the caller's frame buffer on
the device — nothing is copied and nothing is allocated, which is what makes
50 audio chunks a second affordable. The data is valid only while that buffer
is.

Both codecs are fuzzed: 20,000 iterations of arbitrary bytes through every
decoder. Under `./tools/build.sh asan` that also proves the C side never reads
out of bounds.

## 5. Messages

| id | name | from | purpose |
|---|---|---|---|
| 1 | `hello` | device | identify, and present a stored token |
| 2 | `hello_ack` | daemon | accept, or demand pairing; carries the wall clock |
| 3 | `pair` | device | the one-time code the user typed |
| 4 | `pair_result` | daemon | grant a long-lived token, or refuse |
| 5 / 6 | `ping` / `pong` | both | liveness; a dead socket does not always say so |
| 16 | `audio_chunk` | device | 20 ms of 16 kHz mono PCM |
| 17 / 18 | `transcript_partial` / `_final` | daemon | streamed and settled transcription |
| 32 | `agent_request` | device | a turn of conversation |
| 33 / 34 | `agent_token` / `agent_done` | daemon | the reply, streamed |
| 48 | `mood_hint` | daemon | the agent's tone drives the face |
| 49 | `notification` | daemon | |
| 50 | `ota_available` | daemon | version, URL, size and SHA-256 |

`mood_hint` is why the bridge and the persona are peers rather than caller and
callee: the bridge publishes `BRIDGE.MOOD_HINT` on the bus and the persona
subscribes. Neither knows the other exists. See ARCHITECTURE.md R1.

## 6. Adding a message

1. Add a `[[message]]` block to `schema/nevos.toml` with an unused id.
2. `./tools/build.sh proto`
3. Handle it where it arrives. Both languages already have the struct, the
   encoder, the decoder and a golden vector.

Adding a **field** to an existing message is steps 1 and 2 only — old peers
keep working by the rules in §3.
