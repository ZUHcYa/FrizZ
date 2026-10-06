#!/bin/bash
# scenes.sh: checks the working tree's FX scene file format (FxScenes.h) round-trips and
# reads hand-written and broken files sensibly, and that a recall's fast slew ends
# (scenes.cpp). Exits 0 when it does.
set -e
T=$(cd "$(dirname "$0")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
DAISYSP=$REPO/firmware/code/libs/DaisySP/Source
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')
mkdir -p "$T/build"
[ -f "$T/build/libdaisysp_host.a" ] || { echo "run ./run.sh once first, it builds DaisySP"; exit 1; }
# the headers with the host MidiClock in place of the real one, as in run.sh
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cp "$REPO/firmware/code/src"/*.h "$D/"
cp "$T/host/MidiClock.h" "$D/"
g++ -O2 -std=gnu++14 -w -I"$D" -I"$T/host" $INC "$T/scenes.cpp" \
    "$T/build/libdaisysp_host.a" -o "$D/scenes"
"$D/scenes"
