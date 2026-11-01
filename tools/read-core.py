#!/usr/bin/env python3
"""Lists the tasks in an ESP32-S3 core dump read from flash: where each stack and TCB sit.

Those addresses are real heap positions, which is how the emulator's heap layout is checked
against the device. Usage: read-core.py <core.bin>. Core dumps stay out of the repo.
"""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
total, version = struct.unpack('<II', data[:8])
elf = data.find(b'\x7fELF')
print(f'dump {total} bytes, version {version:#x}, ELF at {elf}')
base = elf
phoff = struct.unpack('<I', data[base + 28:base + 32])[0]
phentsize, phnum = struct.unpack('<HH', data[base + 42:base + 46])
segments = []
for i in range(phnum):
    kind, offset, vaddr, _, filesz, memsz, _, _ = struct.unpack('<8I', data[base + phoff + i * phentsize:base + phoff + (i + 1) * phentsize])
    if kind == 1:
        segments.append((vaddr, memsz, base + offset))
segments.sort()


def read(address, size):
    for vaddr, length, offset in segments:
        if vaddr <= address and address + size <= vaddr + length:
            return data[offset + address - vaddr:offset + address - vaddr + size]
    return None


# FreeRTOS TCB on this build: pxTopOfStack, ... pxStack at a fixed offset, pcTaskName after it.
for vaddr, length, offset in segments:
    if length not in (356, 360, 364, 368, 372, 376, 380, 384, 388, 392, 400, 408, 412):
        continue
    tcb = data[offset:offset + length]
    for name_at in range(40, min(120, length - 16), 4):
        name = tcb[name_at:name_at + 16].split(b'\0')[0]
        if 3 <= len(name) <= 15 and all(32 <= c < 127 for c in name):
            stack = struct.unpack('<I', tcb[name_at - 4:name_at])[0]
            top = struct.unpack('<I', tcb[:4])[0]
            print(f'{name.decode():16} tcb {vaddr:#x} stack start {stack:#x} current sp {top:#x} used {(stack and top - stack)}')
            break
print('segments:', len(segments), 'lowest', hex(segments[0][0]), 'highest', hex(segments[-1][0]))
