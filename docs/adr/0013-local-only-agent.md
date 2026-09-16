# ADR 0013 — The agent is local-only, and there is no cloud backend

**Status:** accepted (M6)

## Context

NEVOS needs a language model to answer questions and a speech recogniser to
hear them. Neither fits on an ESP32-S3, so both run on the companion daemon on
the user's own computer. The question was whether that daemon should call a
hosted model.

The straightforward version — the daemon holds an API key and forwards
questions to a provider — was rejected on two grounds, and the first is the one
that settled it.

**Cost.** This is a device that sits on a desk and gets spoken to all day, by
whoever walks past. Per-token billing turns idle chatter into a bill, which
makes the honest thing to tell an owner "try not to talk to it too much". A
companion you are discouraged from using is not a companion.

**Trust.** The device has a microphone and lives in a home. "Your audio goes to
a company you have not heard of" is a claim no amount of policy text makes
comfortable. "It runs on your computer and the daemon refuses to connect to
anything else" is checkable.

## Decision

The agent backend is a trait with exactly one real implementation, and that
implementation talks to a model server on loopback. No cloud client is written
— not disabled behind a flag, not present and unused. There is nothing to
enable.

The refusal is enforced in the transport, not in configuration: a host that is
not a loopback literal is an error both when the agent is constructed and again
when a connection is attempted. `NEVOS_OLLAMA_HOST=some.remote.host` fails at
startup rather than quietly forwarding a household's transcripts.

The default is Ollama with a small instruct model (`llama3.2:3b`). That is a
one-command install on all three desktop platforms, answers in about a second
on a laptop CPU, and is comfortably good enough for two sentences of
conversation. The trait exists so that llama.cpp's server, or whatever replaces
it, is a file rather than a redesign.

Speech recognition is the same shape: a trait, a local whisper.cpp backend, and
`is_local()` on the trait so the tray states where audio is processed by asking
the backend rather than by trusting a config file.

## Consequences

- A NEVOS costs nothing to talk to beyond electricity, and keeps working when
  the internet does not.
- Reply quality is whatever a 3B model can manage. The character prompt asks
  for two sentences, which is both the screen's constraint and roughly the
  length such a model is reliably good at.
- A first run fails in a specific way — Ollama not installed, or the model not
  pulled — so the daemon checks at startup and names it, rather than surfacing
  a 404 in the middle of a conversation.
- Nothing in the firmware or the daemon ever holds a provider credential, which
  is the original brief's fourth constraint discharged by construction rather
  than by discipline.
- If a hosted model is ever wanted, it is a new crate implementing the trait
  with `is_local()` returning false, and the tray already shows that difference.
