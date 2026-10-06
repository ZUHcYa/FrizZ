#!/bin/bash
# randomizer.sh: checks the working tree's randomizer on the host (randomizer.cpp): its patterns, chance, pulse width and shift, the effects its gates pick, and FxChain handing them over and back (FxRandomizer.h, FxChain.h). Exits 0 when it passes.
source "$(dirname "$0")/lib.sh"
unit_test randomizer
