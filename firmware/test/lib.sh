#!/bin/bash
# lib.sh: what the test scripts share; sourced, not run. Sets T (this folder), REPO, INC (the
# DaisySP include flags) and BUILD, builds DaisySP for the host once, and provides
# unit_test NAME: builds NAME.cpp against the working tree's headers, with the host
# MidiClock in place of the real one (it opens MIDI), and runs it.
set -e -o pipefail
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
DAISYSP=$REPO/firmware/code/libs/DaisySP/Source
BUILD=$T/build
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')
mkdir -p "$BUILD"

# DaisySP for the host, once
if [ ! -f "$BUILD/libdaisysp_host.a" ]; then
    echo "building DaisySP for the host, once"
    mkdir -p "$BUILD/daisysp"
    for f in $(find "$DAISYSP" -name '*.cpp'); do
        g++ -O2 -std=gnu++14 -w -c "$f" $INC -o "$BUILD/daisysp/$(basename "$f" .cpp).o"
    done
    ar rcs "$BUILD/libdaisysp_host.a" "$BUILD"/daisysp/*.o
fi

# NAME.cpp's warnings are shown, the headers' only if it doesn't build: then every message is
unit_test()
{
    local name=$1 dir log
    dir=$(mktemp -d)
    trap "rm -rf '$dir'" EXIT
    cp "$REPO/firmware/code/src"/*.h "$dir/"
    cp "$T/host/MidiClock.h" "$dir/"
    log=$dir/build.log
    if ! g++ -O2 -std=gnu++14 -Wall -Wno-unused-function -Wno-unused-variable -I"$dir" \
        -I"$T/host" $INC "$T/$name.cpp" "$BUILD/libdaisysp_host.a" -o "$dir/$name" 2> "$log"; then
        cat "$log"
        echo "$name.cpp didn't build"
        exit 1
    fi
    grep -A3 "$name.cpp" "$log" || true
    "$dir/$name"
}
