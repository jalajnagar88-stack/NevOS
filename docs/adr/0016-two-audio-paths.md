# ADR 0016 — A note and a meeting are two paths, not one with a bigger buffer

**Status:** accepted (M7)

## Context

NEVOS captures audio for two quite different reasons. A dictated note is a few
seconds: press, speak, release. A meeting runs for an hour with the device face
down on a table.

The tempting implementation is one capture path with a larger buffer for the
long case.

## Decision

Two paths, chosen by `audio_chunk.kind` on the wire.

A **note** accumulates in the daemon's session, is transcribed in one piece when
the final chunk arrives, and is filed as a note. One piece matters: a
transcriber given a whole utterance produces noticeably better text than one
given the same audio in slices, because the model has the sentence.

A **meeting** is never accumulated at all. Each chunk leaves the session struct
on the call it arrives on and goes to a task that transcribes thirty seconds at
a time. Thirty is a compromise between those two facts — long enough that
whisper has sentences to work with, short enough that words appear on the device
while the meeting is still happening, and small enough that memory holds under a
megabyte where the whole meeting would be a hundred.

The device times its own markers. The daemon is a segment behind — still
transcribing what was said a minute ago — so a marker timestamped there would
land in the middle of a different sentence.

## Consequences

The 120-second cap on an utterance stays, and now means what it says: a stuck
button on the note path cannot exhaust the daemon's memory, because the long
case no longer has to fit through it.

Two failures had to be decided rather than discovered. A segment that fails to
transcribe writes `[…]` into the text and the meeting continues, because one bad
thirty seconds must not end an hour and a transcript with an unmarked hole is
worse than one that admits to it. A meeting whose device dies mid-sentence is
still filed, because what was said before the battery ran out is the only copy
there is.

On the device, a capture survives both the app closing and the daemon
disconnecting. Meeting mode exists to run while the device is doing something
else, and stopping because someone swiped home would be the worst possible
reading of that gesture. Audio that cannot be sent is dropped rather than
queued — it is worth less every second it waits, and the sequence number tells
the daemon exactly what is missing.
