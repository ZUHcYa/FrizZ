#!/bin/bash
# controls.sh: checks the working tree's play page's FX and scene logic (FxControls.h, SceneControls.h) on the host (controls.cpp). Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test controls
