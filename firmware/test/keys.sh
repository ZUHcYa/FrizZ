#!/bin/bash
# keys.sh: checks the working tree's CHOMPI, PLAY and LOOP keys (PlayKeys.h) on the host (keys.cpp): the confirm tap, SHIFT combos, the looper's combos. Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test keys
