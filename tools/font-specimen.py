#!/usr/bin/env python3
"""Builds and runs the font specimen, then writes one contact sheet at 2x and a measurement table.

Usage: tools/font-specimen.py <output folder>
Pixels are real M5GFX output, scaled by whole numbers only. Readability is judged on the device.
"""
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIDTH, HEIGHT, SCALE, COLUMNS, GAP = 240, 135, 2, 3, 6


def png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + row for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def rows_of(path):
    data = path.read_bytes()
    rows = []
    for y in range(HEIGHT):
        row = bytearray()
        for x in range(WIDTH):
            # LovyanGFX keeps 16 bit sprites byte-swapped.
            high, low = data[(y * WIDTH + x) * 2], data[(y * WIDTH + x) * 2 + 1]
            color = high << 8 | low
            row += bytes(((color >> 11 & 31) * 255 // 31, (color >> 5 & 63) * 255 // 63, (color & 31) * 255 // 31)) * SCALE
        rows += [bytes(row)] * SCALE
    return rows


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(["make", "-s", "-j12", "font-specimen"], cwd=ROOT, check=True)
    table = subprocess.run([ROOT / "build/font-specimen/font-specimen", out], check=True, capture_output=True, text=True).stdout
    (out / "fonts.tsv").write_text(table)
    print(table, end="")
    screens = sorted(out.glob("*.rgb565"))
    tile_w, tile_h = WIDTH * SCALE, HEIGHT * SCALE
    rows_needed = (len(screens) + COLUMNS - 1) // COLUMNS
    sheet_w = COLUMNS * tile_w + (COLUMNS + 1) * GAP
    gray = bytes((60, 60, 60))
    rows = [gray * sheet_w] * GAP
    for start in range(0, len(screens), COLUMNS):
        tiles = [rows_of(path) for path in screens[start:start + COLUMNS]]
        for y in range(tile_h):
            row = gray * GAP
            for column in range(COLUMNS):
                row += (tiles[column][y] if column < len(tiles) else gray * tile_w) + gray * GAP
            rows.append(row)
        rows += [gray * sheet_w] * GAP
    png(out / "fonts.png", sheet_w, rows_needed * tile_h + (rows_needed + 1) * GAP, rows)
    for path in screens:
        path.unlink()
    print(f"sheet: {out / 'fonts.png'}")


main()
