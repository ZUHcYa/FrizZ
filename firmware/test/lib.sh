#!/bin/bash
# lib.sh: what the test scripts share; sourced, not run. Sets T (this folder), REPO, INC (the
# DaisySP include flags) and BUILD, builds DaisySP for the host when needed, and provides
# units (the unit checks' names) and unit_test NAME: builds NAME.cpp against the working tree's headers, with the host
# MidiClock in place of the real one (it opens MIDI), and runs it.
set -e -o pipefail
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(git -C "$T" rev-parse --show-toplevel)
DAISYSP=$REPO/firmware/code/libs/DaisySP/Source
BUILD=$T/build
INC=$(find "$DAISYSP" -type d | sed 's/^/-I/' | tr '\n' ' ')
mkdir -p "$BUILD"

# DaisySP for the host, rebuilt when its sources or the compiler change
STAMP=$( (g++ --version; find "$DAISYSP" -name '*.cpp' -o -name '*.h' | sort | xargs cat) | md5sum | cut -d' ' -f1)
if [ ! -f "$BUILD/libdaisysp_host.a" ] || [ "$(cat "$BUILD/daisysp.stamp" 2>/dev/null)" != "$STAMP" ]; then
    echo "building DaisySP for the host"
    rm -rf "$BUILD/daisysp" "$BUILD/libdaisysp_host.a"
    mkdir -p "$BUILD/daisysp"
    for f in $(find "$DAISYSP" -name '*.cpp'); do
        g++ -O2 -std=gnu++14 -w -c "$f" $INC -o "$BUILD/daisysp/$(basename "$f" .cpp).o"
    done
    ar rcs "$BUILD/libdaisysp_host.a" "$BUILD"/daisysp/*.o
    echo "$STAMP" > "$BUILD/daisysp.stamp"
fi

# the unit checks: every NAME.cpp but the harness
units()
{
    for f in "$T"/*.cpp; do
        f=$(basename "$f" .cpp)
        [ "$f" = harness ] || echo "$f"
    done
}

# NAME.cpp's warnings are shown, the headers' only if it doesn't build: then every message is.
# A check that includes twin.h runs on the virtual CHOMPI instead: the whole firmware, built by
# ../twin/build.sh
unit_test()
{
    local name=$1 dir log
    dir=$(mktemp -d)
    trap "rm -rf '$dir'" EXIT
    if grep -q '#include "twin.h"' "$T/$name.cpp"; then
        "$REPO/firmware/twin/build.sh"
        log=$dir/build.log
        if ! g++ -O2 -std=gnu++14 -Wall -I"$REPO/firmware/twin" "$T/$name.cpp" \
            "$REPO/firmware/twin/build/libtwin.a" "$BUILD/libdaisysp_host.a" -o "$dir/$name" 2> "$log"; then
            cat "$log"
            echo "$name.cpp didn't build"
            exit 1
        fi
        cat "$log"
        "$dir/$name"
        return
    fi
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
