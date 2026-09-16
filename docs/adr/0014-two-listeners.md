# ADR 0014 — The device link and the control API are separate listeners

**Status:** accepted (M6)

## Context

The daemon serves two very different callers. The device connects over the
local network, because the robot is across the room. The desktop tray calls a
small JSON API to show status, complete pairing, list notes and erase
everything.

The obvious implementation is one HTTP server with a WebSocket route and some
JSON routes next to it.

## Decision

Two listeners.

- The device link binds `0.0.0.0` on port 4821 and serves exactly one route,
  `/ws`, plus a health check.
- The control API binds `127.0.0.1` on port 4822 and refuses to start on any
  other address — checked in code, with the reason in the error message.

## Consequences

One server would have meant `POST /api/purge/all` answering the whole network:
every device on the Wi-Fi able to list a household's notes and delete them, with
no authentication in front of it because the tray is trusted by being local.
Adding auth to the control API would be the other way out, but a credential the
tray has to store is a worse answer than an address nobody else can reach.

The cost is two ports and one more line of firewall configuration. The pairing
flow crosses between them — the device presents a code on one, the user enters
it on the other — which is not an accident: that crossing is what proves
someone is in the room.
