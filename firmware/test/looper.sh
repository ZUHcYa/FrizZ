#!/bin/bash
# looper.sh: checks the working tree's the looper (Looper.h): recording, playback, pause, erase, the speed ladder on the host (looper.cpp). Exits 0 when it passes.
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
g++ -O2 -std=gnu++14 -Wall -Wno-unused-function -Wno-unused-variable -I"$D" -I"$T/host" $INC "$T/looper.cpp" \
    "$T/build/libdaisysp_host.a" -o "$D/looper" 2>&1 | grep -v "^$" | grep "looper.cpp" || true
"$D/looper"
