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
# Short tone files, so that end-of-chapter behaviour can be tested in seconds and in public.
if command -v ffmpeg >/dev/null; then
  fixture="$(dirname "$run")/.fixture"
  if [ ! -d "$fixture" ]; then
    mkdir -p "$fixture/books/Aaa Test Book" "$fixture/music/Aaa Test Album"
    for n in 1 2 3; do
      ffmpeg -v error -y -f lavfi -i "sine=frequency=$((200 + n * 110)):duration=12" -ac 1 -ar 22050 -c:a libmp3lame -b:a 48k -write_xing 0 -id3v2_version 0 "$fixture/books/Aaa Test Book/00$n Chapter $n.mp3"
      ffmpeg -v error -y -f lavfi -i "sine=frequency=$((300 + n * 110)):duration=8" -ac 2 -ar 44100 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 "$fixture/music/Aaa Test Album/0$n Song $n.mp3"
    done
  fi
  mkdir -p "$run/card/books" "$run/card/music"
  cp -r "$fixture/books/Aaa Test Book" "$run/card/books/"
  cp -r "$fixture/music/Aaa Test Album" "$run/card/music/"
fi
