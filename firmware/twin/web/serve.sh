#!/bin/bash
# serve.sh [PORT]: serves the virtual CHOMPI on http://localhost:PORT (8765). Each page load
# rebuilds it first if the firmware or the twin changed (serve.py), so a reload always plays
# the working tree as it is. Open it in Chrome, Edge or Firefox.
set -e
cd "$(dirname "$0")"
../build.sh wasm
exec python3 serve.py "$@"
