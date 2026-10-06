#!/bin/bash
# looper.sh: checks the working tree's looper (Looper.h) on the host (looper.cpp): recording, playback, pause, erase, the speed ladder. Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test looper
