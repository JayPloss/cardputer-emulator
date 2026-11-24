#!/bin/bash
# Makes a card holding one Morning Overview brief (Kidputer v0.66) for 2026-11-25: its JSON as the server
# serves it, both languages' sound as the generator encodes it (Piper when it is installed, else a tone), and
# the card's index. Test text only, no family data: this repo is public.
# Usage: morning-card.sh <card dir>
set -euo pipefail
card=$1
mkdir -p "$card/morning"
date=2026-11-25
voices=${PIPER_VOICES:-$HOME/.local/share/piper-voices}
fr="Bonjour Testeur! Nous sommes le mercredi 25 novembre. Ce matin, il fait moins sept degrés, ressenti moins onze. Il y a quelques nuages. Il n'y a presque pas de vent. Cet après-midi, il fera jusqu'à un degré. Bonne journée!"
en="Good morning Tester! Today is Wednesday, November 25th. This morning it's minus seven degrees, and it feels like minus eleven. There are a few clouds. There's almost no wind. This afternoon, it will go up to one degree. Have a great day!"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
for lang in fr en; do
  text=${!lang}
  if command -v piper >/dev/null && [ -f "$voices/fr_FR-siwis-medium.onnx" ]; then
    voice=$([ "$lang" = fr ] && echo fr_FR-siwis-medium || echo en_US-lessac-medium)
    echo "$text" | piper -m "$voices/$voice.onnx" -f "$work/$lang.wav" --sentence-silence 0.35 2>/dev/null
  else
    ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=20" "$work/$lang.wav"
  fi
  # The generator's encoding: constant bitrate, no Xing or ID3 header (tools/morning/morning-brief.py).
  ffmpeg -v error -y -i "$work/$lang.wav" -ar 22050 -ac 1 -c:a libmp3lame -b:a 40k -write_xing 0 -id3v2_version 0 -map_metadata -1 "$card/morning/$date.$lang.mp3"
done
seconds() { ffprobe -v error -show_entries format=duration -of csv=p=0 "$1" | cut -d. -f1; }
cat > "$card/morning/$date.json" <<EOF
{"date":"$date","fr":{"weather":"Il fait -7, quelques nuages.","speech":"Bonjour Testeur! Nous sommes le mercredi 25 novembre. Ce matin, il fait -7 degrés, ressenti -11. Il y a quelques nuages. Il n'y a presque pas de vent. Cet après-midi, il fera jusqu'à 1 degré. Bonne journée!","audio_s":$(seconds "$card/morning/$date.fr.mp3")},"en":{"weather":"It's -7, partly cloudy.","speech":"Good morning Tester! Today is Wednesday, November 25th. This morning it's -7 degrees, and it feels like -11. There are a few clouds. There's almost no wind. This afternoon, it will go up to 1 degree. Have a great day!","audio_s":$(seconds "$card/morning/$date.en.mp3")},"events":[{"time":"08:15","fr":"Natation","en":"Swimming"},{"time":"15:30","fr":"Dentiste","en":"Dentist"},{"time":"","fr":"Apporter le livre de lecture","en":"Bring the reading book"}],"version":"test0001"}
EOF
printf '%s\n' "$date" > "$card/morning/index.txt"
