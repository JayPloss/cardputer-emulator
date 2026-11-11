#!/bin/bash
# formats-card.sh [dir]: a card holding one MP3 of each kind real libraries have (music/Formats), made by
# SupaChat's firmware/tools/mp3-fixtures.py, for the card-formats scenario. Needs ffmpeg and uv.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/run/formats-card}
firmware=${FIRMWARE:-$HOME/Work/cardputer-adv-supachat/firmware}
rm -rf "$out"; mkdir -p "$out/music/Formats"
uv run --quiet --with mutagen python -I "$firmware/tools/mp3-fixtures.py" "$out/fx" >/dev/null
n=1
while IFS=$'\t' read -r name expect _; do
  cp "$out/fx/$name" "$out/music/Formats/$(printf %02d $n) $expect ${name%.mp3}.mp3"; n=$((n + 1))
done < "$out/fx/manifest.tsv"
rm -r "$out/fx"
echo "$out"
