#!/bin/bash
# compare.sh [A [B]]: the whole device, two versions side by side. Builds the twin on the
# firmware of A and of B (commits, tags, branches, or "work" for the working tree; default HEAD
# against work), plays every scenario in scenarios/ on both, and compares the master out and
# the LEDs: bit-identical, or from when they part, by how much, and which LEDs.
#
#   ./compare.sh                 # HEAD vs the working tree: a refactor must be bit-identical
#   ./compare.sh v0.10 HEAD      # what changed for a player since v0.10
#   SCENARIOS="looper scenes" ./compare.sh main HEAD
#
# Each version's twin is built once into build/compare/<commit> and kept. Exits 0 when every
# scenario is bit-identical. Uses the twin of the working tree on both firmwares.
set -e -o pipefail
source "$(dirname "$0")/ref.sh"
A=${1:-HEAD}
B=${2:-work}
TA=$(twin_dir "$A")/frizz-twin
TB=$(twin_dir "$B")/frizz-twin
NAMES=${SCENARIOS:-$(cd "$TW/scenarios" && ls *.txt | sed 's/\.txt$//')}
RUNS=$(mktemp -d)
trap "rm -rf '$RUNS'" EXIT
mkdir -p "$RUNS/a" "$RUNS/b"
for n in $NAMES; do
    "$TA" -q -o "$RUNS/a/$n.wav" -l "$RUNS/a/$n.leds" "$TW/scenarios/$n.txt" > /dev/null &
    "$TB" -q -o "$RUNS/b/$n.wav" -l "$RUNS/b/$n.leds" "$TW/scenarios/$n.txt" > /dev/null &
done
wait
echo "$A against $B:"
python3 "$TW/compare.py" "$RUNS/a" "$RUNS/b" $NAMES
