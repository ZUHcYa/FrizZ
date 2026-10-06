#!/bin/bash
# tempo.sh: checks the working tree's FX tempo on the host (tempo.cpp): tap tempo (TapTempo.h), a loop's beats and the tempo clock locked to it (TempoClock.h), a quantized loop's beats (Looper.h), with a faked MIDI clock. Exits 0 when it passes.
set -e
T=$(cd "$(dirname "$0")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
DAISYSP=$REPO/firmware/code/libs/DaisySP/Source
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')
[ -f "$T/build/libdaisysp_host.a" ] || { echo "run ./run.sh once first, it builds DaisySP"; exit 1; }
# the headers with the host MidiClock in place of the real one, as in run.sh
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cp "$REPO/firmware/code/src"/*.h "$D/"
cp "$T/host/MidiClock.h" "$D/"
g++ -O2 -std=gnu++14 -Wall -Wno-unused-function -Wno-unused-variable -I"$D" -I"$T/host" $INC "$T/tempo.cpp" \
    "$T/build/libdaisysp_host.a" -o "$D/tempo" 2>&1 | grep -v "^$" | grep "tempo.cpp" || true
"$D/tempo"
