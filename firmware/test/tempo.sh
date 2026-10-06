#!/bin/bash
# tempo.sh: checks the working tree's FX tempo on the host (tempo.cpp): tap tempo (TapTempo.h), a loop's beats and the tempo clock locked to it (TempoClock.h), a quantized loop's beats (Looper.h), with a faked MIDI clock, and a scene morph landing on its bar lines (FxMorph.h). Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test tempo
