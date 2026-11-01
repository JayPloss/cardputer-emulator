#!/bin/bash
# Builds the firmware commit that boot-looped a real device, its --wrap prefixing each block,
# and shows the same assertion here. Usage: repro-wrap-crash.sh <supachat repo>
set -euo pipefail
repo=${1:?usage: repro-wrap-crash.sh <supachat repo>}
cd "$(dirname "$0")/.."
tree=$(mktemp -d)/96fa870
git -C "$repo" worktree add -q "$tree" 96fa870
make -s -j12 TAG=96fa870 FIRMWARE="$tree/firmware" LIBDEPS="$repo/firmware/.pio/libdeps/cardputer-adv" build
tools/seed.sh run/wrap-crash
"build/96fa870/cardputer-emulator" --card run/wrap-crash/card --state run/wrap-crash/state --scenario scenarios/boot-online.scn --time-scale 5 | grep -E 'on task|PASS|FAILED' || true
git -C "$repo" worktree remove --force "$tree"
