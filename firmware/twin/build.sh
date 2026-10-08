#!/bin/bash
# build.sh: builds the virtual CHOMPI from the working tree into build/: libtwin.a (twin.h's API
# on FRIZZ's own firmware) and frizz-twin (run.sh's script player). Rebuilds only what changed.
#
# The firmware (code/src) and libDaisy's UI, Switch, 4021 and MIDI code are copied unchanged
# into build/tree, with host/ (the board's stand-ins) and test/host/fatfs.h (the card) beside
# them, so every include resolves inside it.
source "$(dirname "$0")/../test/lib.sh"   # T, REPO, INC (DaisySP), the host DaisySP
TW=$REPO/firmware/twin
B=$TW/build
TREE=$B/tree
LIBDAISY=$REPO/firmware/code/libs/libDaisy/src
mkdir -p "$B"

stage()
{
    rm -rf "$TREE.new"
    mkdir -p "$TREE.new/lib" "$TREE.new/src"
    for f in ui/UI.h ui/UI.cpp ui/UiEventQueue.h util/FIFO.h util/Stack.h util/ringbuffer.h \
             dev/sr_4021.h hid/switch.h hid/switch.cpp hid/midi.h hid/midi.cpp \
             hid/midi_parser.h hid/midi_parser.cpp hid/MidiEvent.h; do
        mkdir -p "$TREE.new/lib/$(dirname "$f")"
        cp "$LIBDAISY/$f" "$TREE.new/lib/$f"
    done
    cp -r "$TW/host/." "$TREE.new/lib/"
    cp "$REPO/firmware/test/host/fatfs.h" "$TREE.new/lib/"
    cp "$REPO/firmware/code/src"/*.h "$REPO/firmware/code/src"/*.cpp "$TREE.new/src/"
    cp "$TW"/*.cpp "$TW"/*.h "$TREE.new/src/"
}

stage
FLAGS="-O2 -g -std=gnu++14 -I$TREE/lib -I$TREE/src $INC"
STAMP=$( (g++ --version; echo "$FLAGS"; find "$TREE.new" -type f | sort | xargs cat) | md5sum | cut -d' ' -f1)
if [ -f "$B/frizz-twin" ] && [ "$(cat "$B/twin.stamp" 2>/dev/null)" = "$STAMP" ]; then
    rm -rf "$TREE.new"
    exit 0
fi
rm -rf "$TREE" && mv "$TREE.new" "$TREE"
echo "building the twin"

mkdir -p "$B/obj"
rm -f "$B/obj"/*.o
# the firmware's own files: warnings as the device build would show them aren't the twin's to fix
for f in src/twin.cpp src/encoder.cpp lib/ui/UI.cpp lib/hid/switch.cpp lib/hid/midi.cpp \
         lib/hid/midi_parser.cpp; do
    g++ $FLAGS -w -c "$TREE/$f" -o "$B/obj/$(basename "$f" .cpp).o" &
done
wait
for f in twin encoder UI switch midi midi_parser; do
    [ -f "$B/obj/$f.o" ] || { echo "the twin didn't build"; exit 1; }
done
rm -f "$B/libtwin.a"
ar rcs "$B/libtwin.a" "$B"/obj/*.o
g++ $FLAGS -Wall -c "$TREE/src/cli.cpp" -o "$B/cli.o"
g++ "$B/cli.o" "$B/libtwin.a" "$BUILD/libdaisysp_host.a" -o "$B/frizz-twin"
echo "$STAMP" > "$B/twin.stamp"
