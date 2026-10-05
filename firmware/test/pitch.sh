#!/bin/bash
# pitch.sh: checks the working tree's shifter (FxShifter.h) lands on every interval from
# -12 to +12 semitones without its level wobbling (pitch.cpp). Exits 0 when it does.
set -e
T=$(cd "$(dirname "$0")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
DAISYSP=$REPO/firmware/code/libs/DaisySP/Source
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')
mkdir -p "$T/build"
g++ -O2 -std=gnu++14 -w -I"$REPO/firmware/code/src" -I"$T/host" $INC "$T/pitch.cpp" \
    -o "$T/build/pitch"
"$T/build/pitch"
