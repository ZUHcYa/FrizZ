#!/bin/bash
# controls.sh: checks the working tree's the play page's FX and scene logic (FxControls.h, SceneControls.h) on the host (controls.cpp). Exits 0 when it passes.
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
g++ -O2 -std=gnu++14 -Wall -Wno-unused-function -Wno-unused-variable -I"$D" -I"$T/host" $INC "$T/controls.cpp" \
    "$T/build/libdaisysp_host.a" -o "$D/controls" 2>&1 | grep -v "^$" | grep "controls.cpp" || true
"$D/controls"
