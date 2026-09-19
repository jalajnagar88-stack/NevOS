#!/usr/bin/env bash
#
# The device against the daemon, start to finish.
#
# Everything else in CI tests one side: unit tests for the state machines, the
# codec against golden vectors, a headless render for the UI. None of it would
# have caught a single line of wiring that marked the network down at startup,
# which is exactly what happened — the Wi-Fi state machine's own tests passed
# while the bridge could no longer reach anything.
#
# So this runs the real daemon and the real device code and asserts on what
# comes out the far end: a pairing that needs the code, a streamed answer, a
# meeting transcribed and filed.
#
#   ./tools/build.sh e2e
#
# The daemon address is passed rather than discovered. mDNS is covered by its
# own tests; making the gate depend on multicast working on somebody's CI runner
# would produce a flaky test, and a flaky gate is one people learn to ignore.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

PORT=${NEVOS_E2E_PORT:-4821}
CONTROL=${NEVOS_E2E_CONTROL:-4822}
WORK=$(mktemp -d)
DAEMON_PID=""

cleanup() {
  [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null || true
  # Kept on request, because the useful thing after a failure is the two logs
  # and the data directory, and they are gone by the time anyone reads the
  # error otherwise.
  if [ "${NEVOS_E2E_KEEP:-0}" = "1" ]; then
    echo "e2e: logs kept in $WORK" >&2
  else
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

fail() { echo "e2e: FAIL — $1" >&2; exit 1; }
step() { echo; echo "==> $1"; }

# Counting files in a directory that may not exist yet. Written as a function
# because `ls missing | wc -l` fails the whole script under pipefail, silently,
# which cost two runs to work out the first time.
count_files() { find "$1" -type f 2>/dev/null | wc -l | tr -d ' '; }

curl_json() {
  curl -sS --noproxy '*' --max-time 10 "$@"
}

# True when the daemon lists this device as paired. Parsed rather than grepped:
# the device id also appears under pending_pairings while the code is on screen,
# and a substring match there would report a device as paired the moment it
# asked to be — which is the one thing this test is trying to disprove.
is_paired() {
  curl_json "http://127.0.0.1:$CONTROL/api/status" | python3 -c '
import json, sys
want = sys.argv[1]
status = json.load(sys.stdin)
sys.exit(0 if any(d["device_id"] == want for d in status["devices"]) else 1)
' "$1"
}

# ---------------------------------------------------------------- build

step "building"
./tools/build.sh test >/dev/null
cmake -S targets/host -B build/host-headless -GNinja -DNEVOS_DISPLAY=headless \
  -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build/host-headless >/dev/null
(cd companion && cargo build -q -p nevosd)

SIM=./build/host-headless/nevos_sim
DAEMON=./companion/target/debug/nevosd

# ---------------------------------------------------------------- daemon

step "starting the daemon"
# --mock: canned agent and transcriber, so this needs no model installed.
# --no-mdns: see the note at the top about multicast.
"$DAEMON" --mock --no-mdns --data-dir "$WORK/data" --name ci-box \
  --port "$PORT" --control-port "$CONTROL" > "$WORK/daemon.log" 2>&1 &
DAEMON_PID=$!

for _ in $(seq 1 50); do
  if curl_json "http://127.0.0.1:$CONTROL/api/status" >/dev/null 2>&1; then break; fi
  sleep 0.2
done
curl_json "http://127.0.0.1:$CONTROL/api/status" > "$WORK/status.json" \
  || fail "the daemon never answered on 127.0.0.1:$CONTROL"

grep -q '"local":true' "$WORK/status.json" \
  || fail "the daemon reports a backend that is not local"
echo "    daemon up, both backends local"

export NEVOS_DAEMON="127.0.0.1:$PORT"

# ------------------------------------------------- protocol, end to end

step "the bridge probe: discovery, pairing, an agent turn, a reconnect"
./build/tests/bridge_live > "$WORK/probe.log" 2>&1 \
  || { tail -20 "$WORK/probe.log"; fail "bridge_live did not complete"; }
grep -q "^OK" "$WORK/probe.log" || fail "bridge_live did not report OK"
grep -q "paired and connected" "$WORK/probe.log" || fail "the probe never paired"
grep -q "reconnected without pairing" "$WORK/probe.log" \
  || fail "the stored token did not work on a second connection"
echo "    $(grep 'reply:' "$WORK/probe.log" | head -1)"

# --------------------------------------------------- the whole device

step "the simulator: pair, then record a meeting"
SETTINGS="$WORK/sim-settings.txt"
printf '# e2e\nbrightness=70\nvolume=0\n' > "$SETTINGS"

# Record at frame 120, stop at 500, which is about twelve seconds of audio.
stdbuf -oL "$SIM" --bridge --app meeting --frames 620 \
  --tap 240,303,120 --tap 240,303,500 \
  --settings "$SETTINGS" --shot "$WORK/meeting.ppm" > "$WORK/sim.log" 2>&1 &
SIM_PID=$!

CODE=""
for _ in $(seq 1 60); do
  CODE=$(grep -o "enter [0-9]\{6\}" "$WORK/sim.log" 2>/dev/null | head -1 | cut -d' ' -f2 || true)
  [ -n "$CODE" ] && break
  sleep 0.25
done
[ -n "$CODE" ] || { tail -20 "$WORK/sim.log"; fail "the device never showed a pairing code"; }
echo "    device is showing $CODE"

# This device must not be paired before a human types its code — that is the
# entire point of the code, so it is asserted rather than assumed. (The probe
# above paired a device of its own, so the question is about this one, not
# about the store being empty.)
DEVICE_ID=$(grep -o "this device is nev-[0-9a-f]*" "$WORK/sim.log" | head -1 | awk '{print $NF}')
[ -n "$DEVICE_ID" ] || fail "the device never reported its id"
if is_paired "$DEVICE_ID"; then
  fail "$DEVICE_ID paired without the code being entered"
fi

curl_json -X POST -H 'Content-Type: application/json' \
  -d "{\"code\":\"$CODE\"}" "http://127.0.0.1:$CONTROL/api/pair" | grep -q '"granted":true' \
  || fail "the daemon refused the code the device was showing"
is_paired "$DEVICE_ID" || fail "the code was accepted but $DEVICE_ID is not paired"
echo "    code accepted, $DEVICE_ID paired"

wait "$SIM_PID" || true

# ------------------------------------------- push to talk, and a notification

# A second run of the same device. The pairing is in the settings file, so this
# reconnects with its token rather than showing a code again — which is also
# worth proving, and was not, because the first run is the only one that ever
# pairs.
step "holding the button: a dictated note, and a message from the computer"

stdbuf -oL "$SIM" --bridge --app notes --frames 360 --hold a,90,260 \
  --settings "$SETTINGS" --shot "$WORK/notes.ppm" > "$WORK/notes.log" 2>&1 &
NOTES_PID=$!

# Send the notification once the device is actually connected; sending it before
# there is a socket would prove nothing about the device at all.
SENT=0
for _ in $(seq 1 60); do
  if grep -q "connected" "$WORK/notes.log" 2>/dev/null; then
    if curl_json -X POST -H 'Content-Type: application/json' \
      -d '{"title":"The build finished","body":"all green","urgent":false}' \
      "http://127.0.0.1:$CONTROL/api/notify" | grep -q '"sent_to":[1-9]'; then
      SENT=1
      break
    fi
  fi
  sleep 0.25
done
[ "$SENT" = "1" ] || { tail -20 "$WORK/notes.log"; fail "the daemon could not deliver a notification"; }

wait "$NOTES_PID" || true

if grep -q "enter [0-9]\{6\}" "$WORK/notes.log"; then
  fail "the device asked to pair again instead of using its stored token"
fi

# The gesture reached the microphone. 170 frames of holding is about five
# seconds, well past the 350 ms floor, so a discard here is a real failure
# rather than a timing accident.
grep -q "capture .* started (note)" "$WORK/notes.log" \
  || { tail -20 "$WORK/notes.log"; fail "holding the button never opened the microphone"; }

grep -q "notification: The build finished" "$WORK/notes.log" \
  || { tail -20 "$WORK/notes.log"; fail "the notification reached the daemon but not the screen"; }

NOTES=$(count_files "$WORK/data/notes")
[ "$NOTES" -ge 1 ] || fail "the dictated note was not filed"
echo "    dictated a note, filed $NOTES; the notification reached the screen"

# ---------------------------------------------------------- assertions

step "what came out the far end"

grep -q "capture started" "$WORK/daemon.log" || fail "the daemon never saw a capture start"
grep -q "capture finished" "$WORK/daemon.log" || fail "the capture never finished"

TRANSCRIPTS=$(count_files "$WORK/data/transcripts")
[ "$TRANSCRIPTS" -ge 1 ] || fail "the meeting was not filed as a transcript"

# What was actually written, rather than what the log said about it.
python3 - "$WORK/data/transcripts" <<'PYCHECK' || fail "the filed transcript is not usable"
import json, pathlib, sys
files = sorted(pathlib.Path(sys.argv[1]).glob("*.json"))
record = json.loads(files[-1].read_text())
assert record["kind"] == "transcript", record["kind"]
assert record["text"].strip(), "the transcript has no text in it"
print(f"    filed as {record['kind']}: {record['text'][:60]!r}")
PYCHECK

# The device's own health, from the run that just happened. Written as `if`
# rather than `grep && fail`, because under `set -e` a grep that finds nothing
# ends the script silently — which is the opposite of what a gate should do.
if grep -q "leaked" "$WORK/sim.log"; then fail "the device leaked a buffer"; fi
if grep -q "no blob for audio" "$WORK/sim.log"; then
  fail "the device ran out of buffers mid-capture"
fi

OVER=$( (grep -o "[0-9]* over budget" "$WORK/sim.log" || true) | tail -1 | cut -d' ' -f1)
[ "${OVER:-0}" = "0" ] || fail "$OVER frames went over the 33 ms budget"
echo "    $( (grep -o '[0-9]* frames — .*' "$WORK/sim.log" || true) | tail -1)"

# And the purge, because a control that is never exercised is a control that
# does not work.
curl_json -X POST "http://127.0.0.1:$CONTROL/api/purge/all" | grep -q '"removed"' \
  || fail "the purge control failed"
REMAINING=$(count_files "$WORK/data/transcripts")
[ "$REMAINING" = "0" ] || fail "the purge left $REMAINING transcript(s) behind"
REMAINING_NOTES=$(count_files "$WORK/data/notes")
[ "$REMAINING_NOTES" = "0" ] || fail "the purge left $REMAINING_NOTES note(s) behind"
# Erasing your notes must not also unpair the robot on your desk.
is_paired "$DEVICE_ID" || fail "the purge unpaired the device, which it must not"
echo "    purge erased the transcripts and kept the pairing"

echo
echo "e2e: the device paired, answered, recorded a meeting, dictated a note,"
echo "     took a notification, and purged everything it had kept."
