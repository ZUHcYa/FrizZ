#!/bin/bash
# all.sh: runs every host check on the working tree: the engine against HEAD (check.sh), then
# each unit check (every NAME.cpp but the harness). Prints one line per check and exits 0 when
# all pass, with the faults the checks know of (Known() in check.h) listed under theirs. A
# changed sound (check.sh not bit-identical) counts as a failure: run ./check.sh to
# see what changed.
source "$(dirname "$0")/lib.sh"
set +e
fail=0
for t in check $(units); do
    if [ "$t" = check ]; then out=$("$T/check.sh" 2>&1); else out=$("$T/unit.sh" "$t" 2>&1); fi
    if [ $? -eq 0 ]; then
        echo "ok    $t: $(echo "$out" | tail -1)$(echo "$out" | grep -m1 -E '^[0-9]+ known$' | sed 's/^/, /')"
        echo "$out" | grep "^KNOWN" | sed 's/^/      /'
    else
        echo "FAIL  $t:"
        echo "$out" | grep -v "^ok" | sed 's/^/      /'
        fail=1
    fi
done
exit $fail
