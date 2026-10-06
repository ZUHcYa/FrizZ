#!/bin/bash
# run.sh <git ref | work> <out.bin>
# Builds the harness against FRIZZ's engine headers at that git ref (or the working tree) and
# runs it, writing every output sample and FX meter to <out.bin>. Needs a host g++.
source "$(dirname "$0")/lib.sh"
SRC=firmware/code/src

# that version's headers, with the host MidiClock in place of the real one (it opens MIDI)
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
if [ "$1" = work ]; then
    cp "$REPO/$SRC"/*.h "$D/"
else
    # FRIZZ lived in firmware/frizz/ before the repo was reorganized
    git -C "$REPO" cat-file -e "$1:$SRC" 2>/dev/null || SRC=firmware/frizz/code/src
    for f in $(git -C "$REPO" ls-tree --name-only "$1" "$SRC/" | grep '\.h$'); do
        git -C "$REPO" show "$1:$f" > "$D/$(basename "$f")"
    done
fi
cp "$T/host/MidiClock.h" "$D/"

# -ffp-contract=off: no fused multiply-adds, so builds compare bit for bit
g++ -O2 -std=gnu++14 -ffp-contract=off -w -I"$D" -I"$T/host" $INC "$T/harness.cpp" \
    "$BUILD/libdaisysp_host.a" -o "$D/harness"
"$D/harness" "$2"
