#!/bin/bash
# Boots the real firmware binary in Espressif's ESP32-S3 QEMU: real ROM, bootloader, ESP-IDF
# startup, heap, newlib, FATFS and prebuilt libraries. Fails on any panic, assert or corrupt
# heap, or if setup() doesn't finish. The native emulator can't see those layers.
# Usage: boot-check.sh <firmware dir> [seconds]
set -uo pipefail
firmware=$(realpath "${1:?usage: boot-check.sh <firmware dir> [seconds]}")
seconds=${2:-70}
here=$(dirname "$(readlink -f "$0")")
cache=${CARDPUTER_QEMU_CACHE:-$HOME/.cache/cardputer-qemu}
export PATH="$HOME/.local/bin:$PATH" LD_LIBRARY_PATH="$cache/lib/usr/lib" MTOOLS_SKIP_CHECK=1
"$here/fetch.sh" "$cache" || exit 2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The autopilot stays out of the firmware repo's sources; it is copied in for this build only.
cp "$here/qemu_autopilot.cpp" "$firmware/src/qemu_autopilot.cpp"
trap 'rm -rf "$work"; rm -f "$firmware/src/qemu_autopilot.cpp"' EXIT
(cd "$firmware" && PLATFORMIO_BUILD_SRC_FLAGS="-DQEMU_AUTOPILOT" pio run -e qemu) > "$work/build.log" 2>&1 || { tail -20 "$work/build.log"; echo "boot-check: build failed"; exit 2; }
build="$firmware/.pio/build/qemu"
python=$(uv tool dir)/platformio/bin/python
bootapp=$(find ~/.platformio/packages/framework-arduinoespressif32/tools/partitions -name boot_app0.bin)
"$python" ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 merge_bin --fill-flash-size 8MB --flash_mode dio --flash_size 8MB \
  -o "$work/flash.bin" 0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" 0xe000 "$bootapp" 0x10000 "$build/firmware.bin" > /dev/null || exit 2

ffmpeg -v error -f lavfi -i "sine=330:d=20" -ac 1 -ar 22050 -c:a libmp3lame -b:a 48k -write_xing 0 -id3v2_version 0 -y "$work/test.mp3"

# One boot of the real binary. Sets $log; fails on a crash of any kind.
boot() {
  local name=$1 card="$work/$1.img" flash="$work/$1.bin"
  log="$work/$1.log"
  cp "$work/flash.bin" "$flash"
  truncate -s 64M "$card"
  mkfs.vfat -F 32 "$card" > /dev/null
  printf '1\tfamily\n' > "$work/room.tsv"
  "$cache/tools/usr/bin/mcopy" -i "$card" "$work/room.tsv" ::/supachat-family.tsv
  "$cache/tools/usr/bin/mmd" -i "$card" ::/books ::/books/Test ::/music
  "$cache/tools/usr/bin/mcopy" -i "$card" "$work/test.mp3" "::/books/Test/001 Test.mp3"
  [ "$name" = squeeze ] && "$cache/tools/usr/bin/mcopy" -i "$card" "$work/room.tsv" ::/squeeze
  timeout "$seconds" "$cache/qemu/bin/qemu-system-xtensa" -M esp32s3 -nographic -no-reboot \
    -drive "file=$flash,if=mtd,format=raw" -drive "file=$card,if=sd,format=raw" \
    -serial "file:$log" -monitor none < /dev/null 2>&1 | tail -3
  local status=${PIPESTATUS[0]}
  # 124 is the timeout ending a run that stayed up; anything else but a clean stop is QEMU itself failing.
  if [ "$status" != 124 ] && [ "$status" != 0 ]; then echo "boot-check: QEMU itself exited $status"; exit 2; fi
  grep -E 'AUTOPILOT|SUPACHAT ready' "$log" | cut -c1-120
  if grep -qE 'CORRUPT HEAP|assert failed|abort\(\)|Guru Meditation|Stack (canary|overflow)|Rebooting|Task watchdog' "$log"; then
    echo "boot-check: $name FAILED, the real binary crashed"
    grep -vE '^$|AUTOPILOT|^\[' "$log" | tail -12 | cut -c1-150
    grep -oE '0x4[0-9a-f]{7}' "$log" | head -18 | \
      ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfC -e "$build/firmware.elf" | \
      sed -E 's# at .*/([^/]+:[0-9]+).*# \1#' | grep -v '??'
    exit 1
  fi
  if ! grep -q 'SUPACHAT ready' "$log"; then echo "boot-check: $name FAILED, setup() did not finish"; tail -12 "$log" | cut -c1-150; exit 1; fi
}

boot play
grep -qE 'AUTOPILOT done played=[01] state=ENDED position=(19|20)' "$log" || { echo "boot-check: play FAILED, the book didn't play to its end"; exit 1; }
grep -q 'AUTOPILOT end' "$log" || { echo "boot-check: play FAILED, stopping the player's task hung"; exit 1; }

boot squeeze
grep -qE 'AUTOPILOT done played=0 state=FAILED.*NO MEMORY' "$log" || { echo "boot-check: squeeze FAILED, running out of memory wasn't reported as NO MEMORY"; exit 1; }

echo "boot-check: PASS, real binary: setup() finished, a book played to its end, running out of memory is reported, no crash"
echo "boot-check: NOT COVERED by this run: Wi-Fi and its RAM (the driver isn't emulated, so free memory here is higher than on the device), SD over SPI, screen, keyboard, I2S sound, and the watchdog restart (a hang shows as a test failure, not a reboot)"
