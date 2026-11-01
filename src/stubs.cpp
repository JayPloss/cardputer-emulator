// M5Cardputer's Keyboard.cpp names its hardware readers. They are never used here.
#include <Arduino.h>

// newlib has these; glibc doesn't.
extern "C" char *itoa(int value, char *out, int radix) { return ltoa(value, out, radix); }
extern "C" char *utoa(unsigned value, char *out, int radix) { return ultoa(value, out, radix); }

// libhelix's allocator, as the real library behaves: out of memory, it spins forever. The firmware
// has to wrap this (see Makefile); a build that doesn't reaches this hang under the watchdog model.
extern "C" void *helix_malloc(int size) {
  void *p = calloc(1, size ? size : 1);
  while (!p) {}
  return p;
}
extern "C" void helix_free(void *p) { free(p); }

// libc's _free_r, as the firmware's hook reaches it: straight to the heap.
struct _reent;
extern "C" void emuArenaFree(void *);
extern "C" void emuFreeR(struct _reent *, void *block) { emuArenaFree(block); }
