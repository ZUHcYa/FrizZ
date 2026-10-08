#!/bin/bash
# run.sh [-o out.wav] [-l leds.txt] SCRIPT: builds the virtual CHOMPI from the working tree if
# it changed, then plays SCRIPT into it from power-on (README.md has the commands)
set -e
"$(dirname "$0")/build.sh"
exec "$(dirname "$0")/build/frizz-twin" "$@"
