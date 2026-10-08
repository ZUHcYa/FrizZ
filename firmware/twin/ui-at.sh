#!/bin/bash
# ui-at.sh REF: runs the whole-device checks (../test/ui.cpp, as they are in the working tree)
# on REF's firmware. A new check should fail on the version before its fix and pass after:
#   ./ui-at.sh main     # before this branch's fix: the new check fails
#   ./ui-at.sh work     # with it: everything passes
set -e -o pipefail
source "$(dirname "$0")/ref.sh"
[ -n "$1" ] || { echo "usage: ui-at.sh REF (a commit, tag, branch, or work)"; exit 2; }
DIR=$(twin_dir "$1")
TWIN_FIRMWARE=$DIR/src TWIN_BUILD=$DIR exec "$REPO/firmware/test/unit.sh" ui
