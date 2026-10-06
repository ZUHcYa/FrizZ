#!/bin/bash
# tape.sh: checks the working tree's wow & flutter (FxWarble.h) and tape stop (FxTapeStop.h) on the host (tape.cpp): transparent when neutral, the flutter's depth, stops, spin-ups and the splice back to the input. Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test tape
