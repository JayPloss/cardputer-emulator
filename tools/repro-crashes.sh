#!/bin/bash
# Builds the two firmware commits that crashed on a real device and shows the crashes reproducing.
# Usage: repro-crashes.sh <supachat repo>
set -euo pipefail
repo=${1:?usage: repro-crashes.sh <supachat repo>}
cd "$(dirname "$0")/.."
for commit in 2b20a87 0cee15c; do
  tree=$(mktemp -d)/$commit
  git -C "$repo" worktree add -q "$tree" "$commit"
  make -s -j12 TAG="$commit" FIRMWARE="$tree/firmware" LIBDEPS="$repo/firmware/.pio/libdeps/cardputer-adv" build
  tools/seed.sh "run/$commit" "${CARD:-$HOME/Cardputer-card}"
  "build/$commit/cardputer-emulator" --card "run/$commit/card" --state "run/$commit/state" --scenario "scenarios/crash-$commit.scn" --time-scale 10 | grep -E 'on task|PASS|FAILED' || true
  git -C "$repo" worktree remove --force "$tree"
done
