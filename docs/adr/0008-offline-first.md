# ADR 0008 — The device is fully useful without the daemon

**Status:** accepted (M1, shapes M3 onward)

## Context
NEVOS pairs with a daemon on the user's PC. Wi-Fi drops, laptops sleep, and the
device sits on a desk where it is looked at constantly.

## Decision
Games, `focus`, `clock` and `settings` work standalone. `notes` records locally
and queues for sync. `meeting` captures to LittleFS and uploads later. `agent`
is the only app that hard-requires the link.

## Consequences
An offline queue and a sync reconciliation layer that would not otherwise exist,
and every capture app needs a partial-state UI. In exchange the device is never
a brick on a bad Wi-Fi day, and a thought the user wanted captured is never lost
to a connectivity failure.

This reaches into appkit: app descriptors declare whether they require the
bridge, and the lifecycle manager surfaces that in the home grid rather than
letting an app fail after launch.
