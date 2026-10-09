#!/bin/sh
# Installs FRIZZ's git hooks (pre-commit) into this clone; its worktrees share them.
# The hook is linked to the main checkout's copy, so it follows main.
cd "$(dirname "$0")" || exit 1
common=$(cd "$(git rev-parse --git-common-dir)" && pwd)
main=$(dirname "$common")
mkdir -p "$common/hooks"
ln -sf "$main/firmware/tools/pre-commit" "$common/hooks/pre-commit"
echo "installed $common/hooks/pre-commit -> $main/firmware/tools/pre-commit"
