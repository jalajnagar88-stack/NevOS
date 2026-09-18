# The companion daemon

`nevosd` runs on your own computer and does the three things the device cannot:
transcribe speech, run a language model, and keep files. All three happen
locally. There is no account, no API key, and no cloud service anywhere in the
program — see [ADR 0013](adr/0013-local-only-agent.md) for why that is a
decision rather than an omission.

The device works without it. Pairing, games, the clock, the face and the timer
all run on the ESP32-S3 alone; the daemon adds voice, notes and the agent.

## Running it

```sh
cd companion
cargo run -p nevosd                 # the real thing
cargo run -p nevosd -- --mock       # no model, no whisper, everything else works
cargo run -p nevosd -- --help
```

`--mock` is worth knowing about: it swaps in a canned agent and transcriber so
the whole pipeline — discovery, pairing, audio, transcript, reply, mood — can be
exercised on a machine with nothing installed.

### What it needs installed

| For | Install | Configure |
|-----|---------|-----------|
| the agent | [Ollama](https://ollama.com), then `ollama pull llama3.2:3b` | `NEVOS_MODEL`, or `--model` |
| transcription | a [whisper.cpp](https://github.com/ggerganov/whisper.cpp) build and a model | `NEVOS_WHISPER_BIN`, `NEVOS_WHISPER_MODEL` |

Neither is required to start. A missing model server is a warning at startup and
an error message on the device when you ask something; a missing whisper is a
warning and a voice capture that reports it could not be transcribed. Refusing
to start because one optional binary is absent would take the notes app, the
agent, pairing and the games offline along with it.

## The two listeners

| | Bound to | Port | Serves |
|---|---|---|---|
| device link | `0.0.0.0` | 4821 | `/ws` — the CBOR protocol, and `/health` |
| control API | `127.0.0.1` | 4822 | the tray's JSON API |

They are separate on purpose. The control API can list your notes and erase all
of them, and nothing on the network has any business calling it —
[ADR 0014](adr/0014-two-listeners.md).

## Finding the daemon

The daemon advertises `_nevos._tcp.local.` over mDNS with the device port and a
TXT record carrying the protocol version. The device connects to what it finds.

Discovery is not authentication. Anything on the network can claim to be a
NEVOS daemon; that is what the pairing code is for.

## Pairing

```
  device                          daemon                         you
    │  hello (token: "")            │                              │
    │ ─────────────────────────────>│                              │
    │  hello_ack(needs_pairing)     │                              │
    │ <─────────────────────────────│                              │
    │                               │   shows 424242 on its screen │
    │  pair(code: "424242")         │                              │
    │ ─────────────────────────────>│                              │
    │                               │  ← POST /api/pair {"424242"} │
    │  pair_result(granted, token)  │                              │
    │ <─────────────────────────────│                              │
```

The code goes over the network in one direction and over your eyes in the
other. The daemon grants nothing until both arrive and match, so a device that
is not in the room cannot pair no matter what it sends. Five wrong entries lock
the endpoint for five minutes; an attempt expires after three minutes, and the
device is told when it does so it can stop showing a code that will not work.

Tokens are 32 random bytes as hex, stored on the device in NVS and on this
machine in the data directory. A token is bound to the device id that was issued
it: a token read out of one robot's flash does not authenticate a second one.

## The control panel

Open <http://127.0.0.1:4822/> while the daemon is running. It is one HTML file
embedded in the binary, served on the same loopback listener as the API, so
there is nothing to install and nothing extra to run.

The microphone indicator is the loudest thing on the page on purpose. It is the
one claim this product makes that a person cannot verify for themselves, so when
audio is arriving it takes a band across the top, pulses, and changes the tab
title to `● NEVOS — mic live` — visible even when the tab is not. When audio is
not arriving it says so plainly rather than disappearing: an indicator you only
ever see when something is wrong teaches people to ignore the space where it
lives.

It is driven by audio actually arriving from a device, not by anything
announcing an intention to record.

A menu bar or system tray app would be a nicer front door, and this page is what
it would show. That is a separate piece of work — it needs a GUI toolchain per
platform — and none of the behaviour here depends on it.

### The end-to-end test

```sh
./tools/build.sh e2e
```

Starts a daemon, runs the device's own bridge against it, then runs the whole
simulator: pairing that needs the code typed, a streamed answer, a meeting
recorded and transcribed and filed, and a purge that keeps the pairing. It is
what CI runs, and it exists because the unit tests cannot see wiring — a single
line that marked the network down at startup once broke the bridge entirely
while every state machine's own tests still passed.

`NEVOS_DAEMON=127.0.0.1:4821` points the simulator at a fixed address instead of
discovering one, which is how the test avoids depending on multicast working on
somebody's build machine.

### Trying it without hardware

```sh
cargo run -p nevosd --example fake_device            # pairs, then streams audio
cargo run -p nevosd --example fake_device -- --dictate   # three notes, then exits
```

The fake device pairs itself by reading its own code back through the control
API, which is exactly what a person at the keyboard does. It exists because the
microphone indicator cannot be designed against a state that never occurs.

## Control API

All on `127.0.0.1:4822`.

| Route | Does |
|-------|------|
| `GET /api/status` | daemon name, version, data directory, whether the mic is live, whether each backend is local, paired devices, pending pairings |
| `POST /api/pair` | `{"code": "424242"}` — completes a pairing |
| `POST /api/forget` | `{"device_id": "..."}` — unpairs |
| `GET /api/records?kind=note` | notes, or `kind=transcript` |
| `POST /api/records/delete` | `{"kind": "note", "id": "..."}` — deletes one record and its audio |
| `POST /api/notify` | `{"title": "...", "body": "...", "urgent": false}` — puts a message on every paired device |
| `POST /api/purge/audio` | deletes retained audio, keeps the text |
| `POST /api/purge/all` | deletes every note, transcript and recording, keeps pairing |

`mic_live` is driven by audio actually arriving, not by anything announcing an
intention to record. The tray shows it as the largest thing on the panel.

`records/delete` is there because a purge that can only take everything is not
really a delete control: what people want is to remove the one note that should
not have been recorded, and offering only "keep it or lose the lot" means they
end up with neither.

`purge/all` deliberately keeps device pairings: erasing your notes should not
also mean setting the robot up again. It also clears the in-memory conversation
history, because a purge that left the last ten minutes of talk in RAM would not
be a purge.

Audio is not retained unless you pass `--keep-audio`. The text is what the
features need; a recording of your kitchen is the part worth not keeping.

## Where things live

| Crate | Does | Depends on |
|-------|------|------------|
| `nevos-proto` | the generated CBOR codec | nothing at all |
| `nevos-store` | notes, transcripts, audio, devices as plain files | serde |
| `nevos-stt` | `Transcriber` trait, whisper.cpp backend | tokio |
| `nevos-agent` | `Agent` trait, Ollama backend, mood inference | tokio, serde_json |
| `nevosd` | the two listeners, pairing, mDNS, wiring | axum, mdns-sd |

Inside `nevosd`, `session.rs` is the whole protocol as a state machine with no
socket in it — the same split the firmware uses for its games, for the same
reason. Everything that could go wrong about a connection is decided there
against a clock that is an argument, so it is a unit test rather than something
found by plugging in the robot. `server.rs` is the hands: it reads frames, runs
them through the session, and performs the actions it hands back.

## Data on disk

Under `NEVOS_DATA_DIR`, else `$XDG_DATA_HOME/nevos`, else `~/.local/share/nevos`
(`~/Library/Application Support/nevos` on macOS).

```
devices/       one JSON file per paired device
notes/         one JSON file per dictated note
transcripts/   long-form captures
audio/         WAV files, only with --keep-audio
```

Plain files, one record each, written with a temp-and-rename so a crash mid-save
cannot leave half a record. They can be read, backed up and deleted with
anything; a database would have made "delete my data" a thing you need this
program to do for you.
