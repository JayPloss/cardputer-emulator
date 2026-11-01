#!/bin/bash
# Boots the real firmware binary in Espressif's ESP32-S3 QEMU: real ROM, bootloader, ESP-IDF
# startup, heap, newlib, FATFS and prebuilt libraries. Fails on any panic, assert or corrupt
# heap, or if setup() doesn't finish. The native emulator can't see those layers.
# Usage: boot-check.sh <firmware dir> [seconds]
set -uo pipefail
firmware=$(realpath "${1:?usage: boot-check.sh <firmware dir> [seconds]}")
seconds=${2:-45}
here=$(dirname "$(readlink -f "$0")")
cache=${CARDPUTER_QEMU_CACHE:-$HOME/.cache/cardputer-qemu}
export PATH="$HOME/.local/bin:$PATH" LD_LIBRARY_PATH="$cache/lib/usr/lib" MTOOLS_SKIP_CHECK=1
"$here/fetch.sh" "$cache" || exit 2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

(cd "$firmware" && pio run -e qemu) > "$work/build.log" 2>&1 || { tail -20 "$work/build.log"; echo "boot-check: build failed"; exit 2; }
build="$firmware/.pio/build/qemu"
python=$(uv tool dir)/platformio/bin/python
bootapp=$(find ~/.platformio/packages/framework-arduinoespressif32/tools/partitions -name boot_app0.bin)
"$python" ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 merge_bin --fill-flash-size 8MB --flash_mode dio --flash_size 8MB \
  -o "$work/flash.bin" 0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" 0xe000 "$bootapp" 0x10000 "$build/firmware.bin" > /dev/null || exit 2

truncate -s 64M "$work/card.img"
mkfs.vfat -F 32 "$work/card.img" > /dev/null
printf '1\tfamily\n' > "$work/room.tsv"
"$cache/tools/usr/bin/mcopy" -i "$work/card.img" "$work/room.tsv" ::/supachat-family.tsv
"$cache/tools/usr/bin/mmd" -i "$work/card.img" ::/books ::/music

timeout "$seconds" "$cache/qemu/bin/qemu-system-xtensa" -M esp32s3 -nographic -no-reboot \
  -drive "file=$work/flash.bin,if=mtd,format=raw" -drive "file=$work/card.img,if=sd,format=raw" \
  -serial "file:$work/serial.log" -monitor none < /dev/null 2>&1 | tail -3
status=${PIPESTATUS[0]}
# 124 is the timeout ending a run that stayed up; anything else but a clean stop is QEMU itself failing.
if [ "$status" != 124 ] && [ "$status" != 0 ]; then echo "boot-check: QEMU itself exited $status"; exit 2; fi

if grep -qE 'CORRUPT HEAP|assert failed|abort\(\)|Guru Meditation|Stack (canary|overflow)|Rebooting' "$work/serial.log"; then
  echo "boot-check: FAILED, the real binary crashed"
  grep -vE '^$' "$work/serial.log" | tail -14 | cut -c1-150
  grep -oE '0x4[0-9a-f]{7}' "$work/serial.log" | head -18 | \
    ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfC -e "$build/firmware.elf" | \
    sed -E 's# at .*/([^/]+:[0-9]+).*# \1#' | grep -v '??'
  exit 1
fi
if ! grep -q 'SUPACHAT ready' "$work/serial.log"; then
  echo "boot-check: FAILED, setup() did not finish in $seconds s"
  grep -vE '^$' "$work/serial.log" | tail -14 | cut -c1-150
  exit 1
fi
echo "boot-check: PASS, real binary reached the end of setup() and ran $seconds s with no crash"
