#!/bin/bash
# Puts Espressif's QEMU and the two things it needs in a cache folder, with no root.
set -euo pipefail
cache=$1
release=esp-develop-9.2.2-20260417
archive=qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-x86_64-linux-gnu.tar.xz
mkdir -p "$cache/lib" "$cache/tools"
if [ ! -x "$cache/qemu/bin/qemu-system-xtensa" ]; then
  curl -sL "https://github.com/espressif/qemu/releases/download/$release/$archive" | tar xJ -C "$cache"
fi
if [ ! -e "$cache/lib/usr/lib/libslirp.so.0" ]; then
  curl -sL "$(pacman -Sp libslirp | tail -1)" | tar x --zstd -C "$cache/lib" usr/lib
fi
if [ ! -e "$cache/tools/usr/bin/mcopy" ]; then
  curl -sL "$(pacman -Sp mtools | tail -1)" | tar x --zstd -C "$cache/tools" usr/bin/mcopy usr/bin/mmd usr/bin/mtools
fi
