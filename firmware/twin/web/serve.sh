#!/bin/bash
# serve.sh [PORT]: builds the virtual CHOMPI for the browser if the firmware changed, then serves
# this folder on http://localhost:PORT (8765). Open it in Chrome, Edge or Firefox.
set -e
cd "$(dirname "$0")"
../build.sh wasm
PORT=${1:-8765}
echo "the twin: http://localhost:$PORT  (Ctrl-C stops it)"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
