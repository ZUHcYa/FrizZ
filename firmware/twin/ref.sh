#!/bin/bash
# ref.sh: sourced by compare.sh and ui-at.sh. twin_dir REF builds the twin on REF's firmware (a
# commit, tag or branch; "work" for the working tree) with the working tree's twin around it,
# into build/compare/<commit>, kept for next time, and prints that folder.
TW=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(git -C "$TW" rev-parse --show-toplevel)
OUT=$TW/build/compare

twin_dir()
{
    local ref=$1 dir src
    mkdir -p "$OUT"
    if [ "$ref" = work ]; then
        dir=$OUT/work
        src=$REPO/firmware/code/src
    else
        local hash path
        hash=$(git -C "$REPO" rev-parse --short "$ref^{commit}")
        dir=$OUT/$hash
        src=$dir/src
        if [ ! -d "$src" ]; then
            mkdir -p "$src"
            # FRIZZ moved from firmware/frizz/ to firmware/ (CLAUDE.md)
            path=firmware/code/src
            git -C "$REPO" cat-file -e "$hash:$path" 2> /dev/null || path=firmware/frizz/code/src
            git -C "$REPO" archive "$hash" "$path" \
                | tar -x -C "$src" --strip-components=$(($(echo "$path" | tr -cd / | wc -c) + 1))
        fi
    fi
    TWIN_FIRMWARE=$src TWIN_BUILD=$dir "$TW/build.sh" >&2
    echo "$dir"
}
