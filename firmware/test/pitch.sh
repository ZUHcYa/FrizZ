#!/bin/bash
# pitch.sh: checks the working tree's shifter (FxShifter.h) lands on every interval from
# -12 to +12 semitones without its level wobbling (pitch.cpp). Exits 0 when it does.
source "$(dirname "$0")/lib.sh"
unit_test pitch
