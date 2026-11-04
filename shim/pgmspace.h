// The Arduino core reads a "dword" as an unsigned long, which is 32 bits on the device and 64 here, so
// any font using it (DejaVu, FreeSans) read past each glyph's offset and crashed.
#pragma once
#include_next <pgmspace.h>
#include <stdint.h>
#undef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
