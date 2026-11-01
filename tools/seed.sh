#!/bin/bash
# Makes a run folder: a card of links to the real audio (so writes never touch it) and NVS.
# Usage: seed.sh <run dir> [card source]
set -euo pipefail
run=$1; source=${2:-}
rm -rf "$run"; mkdir -p "$run/card" "$run/state"
if [ -n "$source" ] && [ -d "$source" ]; then
  (cd "$source" && find . -type d) | while read -r d; do mkdir -p "$run/card/$d"; done
  (cd "$source" && find . -type f) | while read -r f; do ln -s "$source/$f" "$run/card/$f"; done
fi
printf 'device_token\temu-token\nwifi_count\t1\nssid0\tEMU-LAN\npsk0\temu\nvolume\t3\nhigh_default\t1\n' > "$run/state/nvs.txt"
