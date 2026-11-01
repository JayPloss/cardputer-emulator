// M5Cardputer's Keyboard.cpp names its hardware readers. They are never used here.
#include <Arduino.h>

// newlib has these; glibc doesn't.
extern "C" char *itoa(int value, char *out, int radix) { return ltoa(value, out, radix); }
extern "C" char *utoa(unsigned value, char *out, int radix) { return ultoa(value, out, radix); }

// libhelix's allocation hooks: plain malloc on a board with no PSRAM.
extern "C" void *helix_malloc(int size) { return malloc(size); }
extern "C" void helix_free(void *p) { free(p); }
