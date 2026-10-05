#!/bin/bash
# run.sh <git ref | work> <out.bin>
# Builds the harness against FRIZZ's engine headers at that git ref (or the working tree) and
# runs it, writing every output sample and FX meter to <out.bin>. Needs a host g++.
set -e
T=$(cd "$(dirname "$0")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
SRC=firmware/frizz/code/src
DAISYSP=$REPO/firmware/frizz/code/libs/DaisySP/Source
BUILD=$T/build
mkdir -p "$BUILD"
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')

# DaisySP for the host, once
if [ ! -f "$BUILD/libdaisysp_host.a" ]; then
    mkdir -p "$BUILD/daisysp"
    for f in $(find "$DAISYSP" -name '*.cpp'); do
        g++ -O2 -std=gnu++14 -w -c "$f" $INC -o "$BUILD/daisysp/$(basename "$f" .cpp).o"
    done
    ar rcs "$BUILD/libdaisysp_host.a" "$BUILD"/daisysp/*.o
fi

# that version's headers, with the host MidiClock in place of the real one (it opens MIDI)
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
if [ "$1" = work ]; then
    cp "$REPO/$SRC"/*.h "$D/"
else
    for f in $(git -C "$REPO" ls-tree --name-only "$1" "$SRC/" | grep '\.h$'); do
        git -C "$REPO" show "$1:$f" > "$D/$(basename "$f")"
    done
fi
cp "$T/host/MidiClock.h" "$D/"

# -ffp-contract=off: no fused multiply-adds, so builds compare bit for bit
g++ -O2 -std=gnu++14 -ffp-contract=off -w -I"$D" -I"$T/host" $INC "$T/harness.cpp" \
    "$BUILD/libdaisysp_host.a" -o "$D/harness"
"$D/harness" "$2"
