#!/bin/bash
# all.sh: runs every host check on the working tree: the engine against HEAD (check.sh), then
# each unit check. Prints one line per check and exits 0 when all pass. A changed sound
# (check.sh not bit-identical) counts as a failure: run ./check.sh to see what changed.
T=$(cd "$(dirname "$0")" && pwd)
fail=0
for t in check pitch scenes controls looper tempo; do
    if out=$("$T/$t.sh" 2>&1); then
        echo "ok    $t: $(echo "$out" | tail -1)"
    else
        echo "FAIL  $t:"
        echo "$out" | grep -v "^ok" | sed 's/^/      /'
        fail=1
    fi
done
exit $fail
