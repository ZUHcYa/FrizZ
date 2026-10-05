#!/bin/bash
# check.sh [ref a] [ref b]: runs the harness on two versions of the engine and compares them.
# Defaults: HEAD against the working tree, the check for a change that shouldn't alter the
# sound. Exits 0 when bit-identical.
set -e
T=$(cd "$(dirname "$0")" && pwd)
A=${1:-HEAD}
B=${2:-work}
mkdir -p "$T/build"
"$T/run.sh" "$A" "$T/build/a.bin"
"$T/run.sh" "$B" "$T/build/b.bin"
echo "$A vs $B:"
python3 "$T/compare.py" "$T/build/a.bin" "$T/build/b.bin"
