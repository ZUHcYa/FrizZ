#!/bin/bash
# comp.sh: checks the working tree's master compressor (MasterComp.h) on the host: off is a
# bypass, its curve, speed, linked stereo and mix, the safety limiter's ceiling, and the
# master settings' file format (MasterSettings.h) (comp.cpp). Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test comp
