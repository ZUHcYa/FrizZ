#!/bin/bash
# scenes.sh: checks the working tree's FX scene file format (FxScenes.h) round-trips and
# reads hand-written and broken files sensibly, and that a recall's fast slew ends
# (scenes.cpp). Exits 0 when it does.
source "$(dirname "$0")/lib.sh"
unit_test scenes
