// The device heap: every allocation the firmware and its libraries make comes out of
// an arena sized like the real one, so running out, fragmentation and largest-free-block
// behave as on the Cardputer. Linked with --wrap for malloc and friends. Blocks carry
// the device's overhead, and under ASan free space is poisoned so stale use is caught.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#include "device_model.h"
#include "emu.h"

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define POISON(p, n) __asan_poison_memory_region((p), (n))
#define UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#define POISON(p, n)
#define UNPOISON(p, n)
#endif

extern "C" {
void *__real_malloc(size_t);
void __real_free(void *);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t);
void __wrap_free(void *);
void *__wrap_calloc(size_t, size_t);
void *__wrap_realloc(void *, size_t);
}

namespace emu {
namespace {
constexpr size_t kAlign = 8;  // Host pointers need 8; the device's own overhead is modelled apart.

// Block layout: Header, payload. Free blocks are found by walking from the start,
// first fit by address, which fragments about as TLSF's good fit does for this load.
struct Header {
  uint32_t size;    // Whole block, header included.
  uint32_t payload;
  uint16_t used;
  uint16_t owner;
  uint32_t magic;
};
constexpr uint32_t kMagic = 0xC0DEB10C;
constexpr size_t kHeader = sizeof(Header);
constexpr uint8_t kTailCanary[4] = {0xBA, 0xAD, 0xF0, 0x0D};

uint8_t *arena = nullptr;
size_t arenaSize = 0;
size_t freeBytes = 0;
size_t lowest = 0;
std::recursive_mutex lock;
thread_local int hostDepth = 0;
thread_local bool hostThread = false;
thread_local uint16_t currentOwner = 0;
constexpr int kOwners = 16;
const char *ownerNames[kOwners] = {"system"};
size_t ownerBytes[kOwners];
int ownerCount = 1;

void ensureArena() {
  if (arena) return;
  const size_t total = model::kHeapMainEnd - emuHeapStart() + model::kHeapExtraRegion;
  arenaSize = total & ~(kAlign - 1);
  arena = static_cast<uint8_t *>(__real_malloc(arenaSize + kAlign));
  arena = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(arena) + kAlign - 1) & ~(kAlign - 1));
  auto *first = reinterpret_cast<Header *>(arena);
  *first = {static_cast<uint32_t>(arenaSize), 0, 0, 0, kMagic};
  freeBytes = lowest = arenaSize;
  POISON(arena + kHeader, arenaSize - kHeader);
}

inline Header *next(Header *block) { return reinterpret_cast<Header *>(reinterpret_cast<uint8_t *>(block) + block->size); }
inline bool last(Header *block) { return reinterpret_cast<uint8_t *>(block) + block->size >= arena + arenaSize; }
inline bool inArena(const void *p) { return arena && p >= arena && p < arena + arenaSize; }

void *arenaAlloc(size_t bytes) {
  std::lock_guard<std::recursive_mutex> guard(lock);
  ensureArena();
  if (bytes > arenaSize) return nullptr;
  // The device's cost for this request, with room for this host's header inside it.
  size_t need = (bytes + sizeof(kTailCanary) + 3) & ~size_t(3);
  need += model::kBlockOverhead;
  if (need < model::kMinBlock) need = model::kMinBlock;
  need = kHeader + ((need - model::kBlockOverhead + (kAlign - 1)) & ~(kAlign - 1)) + (model::kBlockOverhead > kHeader ? model::kBlockOverhead - kHeader : 0);
  need = (need + kAlign - 1) & ~(kAlign - 1);
  for (auto *block = reinterpret_cast<Header *>(arena);; block = next(block)) {
    if (block->used == 0) {
      // Merge free blocks that follow.
      while (!last(block) && next(block)->used == 0) block->size += next(block)->size;
      if (block->size >= need) {
        if (block->size >= need + 64) {
          auto *rest = reinterpret_cast<Header *>(reinterpret_cast<uint8_t *>(block) + need);
          UNPOISON(rest, kHeader);
          *rest = {block->size - static_cast<uint32_t>(need), 0, 0, 0, kMagic};
          POISON(rest + 1, rest->size - kHeader);
          block->size = need;
        }
        block->used = 1;
        block->owner = currentOwner;
        block->payload = bytes;
        ownerBytes[currentOwner] += block->size;
        freeBytes -= block->size;
        if (freeBytes < lowest) lowest = freeBytes;
        UNPOISON(block + 1, bytes + sizeof(kTailCanary));
        memcpy(reinterpret_cast<uint8_t *>(block + 1) + bytes, kTailCanary, sizeof(kTailCanary));
        POISON(reinterpret_cast<uint8_t *>(block + 1) + bytes, sizeof(kTailCanary));
        return block + 1;
      }
    }
    if (last(block)) return nullptr;
  }
}

void arenaFree(void *p) {
  std::lock_guard<std::recursive_mutex> guard(lock);
  auto *block = static_cast<Header *>(p) - 1;
  // As multi_heap_poisoning does on the device: a pointer the allocator never handed out (say, one
  // shifted by an allocation hook, then freed by libc's path past the hook) has no canary before it.
  if (block->magic != kMagic || !block->used) crash("assert multi_heap_poisoning: head != NULL", "CORRUPT HEAP: free() of a pointer with no valid head canary");
  UNPOISON(static_cast<uint8_t *>(p) + block->payload, sizeof(kTailCanary));
  if (memcmp(static_cast<uint8_t *>(p) + block->payload, kTailCanary, sizeof(kTailCanary)) != 0) crash("assert multi_heap_poisoning: tail canary", "CORRUPT HEAP: something wrote past the end of a block");
  block->used = 0;
  ownerBytes[block->owner] -= block->size;
  freeBytes += block->size;
  POISON(p, block->size - kHeader);
}

inline bool useHost() { return hostThread || hostDepth > 0; }
}  // namespace

void setHeapOwner(const char *name) {
  std::lock_guard<std::recursive_mutex> guard(lock);
  for (int i = 0; i < ownerCount; i++) if (!strcmp(ownerNames[i], name)) { currentOwner = i; return; }
  if (ownerCount < kOwners) { ownerNames[ownerCount] = name; currentOwner = ownerCount++; }
}
void heapOwners(char *out, size_t size) {
  std::lock_guard<std::recursive_mutex> guard(lock);
  size_t at = 0;
  for (int i = 0; i < ownerCount && at < size; i++) at += snprintf(out + at, size - at, "%s=%zu ", ownerNames[i], ownerBytes[i]);
}
bool inHostScope() { return useHost(); }
HostScope::HostScope() { hostDepth++; }
HostScope::~HostScope() { hostDepth--; }
void markHostThread() { hostThread = true; }

void *deviceAlloc(size_t bytes, const char *) { return arenaAlloc(bytes); }
void deviceFree(void *block) { if (block) arenaFree(block); }

size_t heapFree() { std::lock_guard<std::recursive_mutex> guard(lock); ensureArena(); return freeBytes; }
size_t heapLowest() { std::lock_guard<std::recursive_mutex> guard(lock); ensureArena(); return lowest; }
size_t heapTotal() { std::lock_guard<std::recursive_mutex> guard(lock); ensureArena(); return arenaSize; }

size_t heapLargest() {
  std::lock_guard<std::recursive_mutex> guard(lock);
  ensureArena();
  size_t best = 0, run = 0;
  for (auto *block = reinterpret_cast<Header *>(arena);; block = next(block)) {
    if (block->used) run = 0;
    else { run += block->size; if (run > best) best = run; }
    if (last(block)) break;
  }
  return best > model::kBlockOverhead ? best - model::kBlockOverhead : 0;
}

void heapReport(const char *when) {
  char owners[400] = "";
  heapOwners(owners, sizeof(owners));
  printf("[heap] %s: free %zu, largest block %zu, lowest ever %zu, of %zu\n[heap]   held by: %s\n", when, heapFree(), heapLargest(), heapLowest(), heapTotal(), owners);
  fflush(stdout);
}

}  // namespace emu

using emu::arenaAlloc;
using emu::arenaFree;
using emu::inArena;
using emu::useHost;

// The allocator proper. The firmware's OS ledger, when the build has one, sits on top of
// these (its __wrap_ functions replace the weak ones here); older firmware calls them directly.
extern "C" {
void *emuArenaMalloc(size_t bytes) {
  if (useHost()) return __real_malloc(bytes);
  if (emu::flakyRefusal()) return nullptr;
  return arenaAlloc(bytes);
}
void emuArenaFree(void *p) {
  if (!p) return;
  if (inArena(p)) arenaFree(p); else __real_free(p);
}
void *emuArenaCalloc(size_t count, size_t size) {
  if (useHost()) return __real_calloc(count, size);
  if (emu::flakyRefusal()) return nullptr;
  void *p = arenaAlloc(count * size);
  if (p) memset(p, 0, count * size);
  return p;
}
void *emuArenaRealloc(void *p, size_t bytes) {
  if (!p) return emuArenaMalloc(bytes);
  if (!inArena(p)) return __real_realloc(p, bytes);
  auto *block = static_cast<emu::Header *>(p) - 1;
  if (bytes <= block->payload) {
    block->payload = bytes;
    memcpy(static_cast<uint8_t *>(p) + bytes, emu::kTailCanary, sizeof(emu::kTailCanary));
    return p;
  }
  void *moved = arenaAlloc(bytes);
  if (!moved) return nullptr;
  memcpy(moved, p, block->payload);
  arenaFree(p);
  return moved;
}
size_t heap_caps_get_allocated_size(void *p) {
  if (!inArena(p)) return 0;
  return (static_cast<emu::Header *>(p) - 1)->payload;
}
__attribute__((weak)) void *__wrap_malloc(size_t bytes) { return emuArenaMalloc(bytes); }
__attribute__((weak)) void __wrap_free(void *p) { emuArenaFree(p); }
__attribute__((weak)) void *__wrap_calloc(size_t count, size_t size) { return emuArenaCalloc(count, size); }
__attribute__((weak)) void *__wrap_realloc(void *p, size_t bytes) { return emuArenaRealloc(p, bytes); }
}

// C++ allocation goes the same way, and throws as on the device when RAM is gone.
void *operator new(size_t bytes) {
  void *p = __wrap_malloc(bytes);
  if (!p) throw std::bad_alloc();
  return p;
}
void *operator new[](size_t bytes) { return operator new(bytes); }
void *operator new(size_t bytes, const std::nothrow_t &) noexcept { return __wrap_malloc(bytes); }
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept { return __wrap_malloc(bytes); }
void operator delete(void *p) noexcept { __wrap_free(p); }
void operator delete[](void *p) noexcept { __wrap_free(p); }
void operator delete(void *p, size_t) noexcept { __wrap_free(p); }
void operator delete[](void *p, size_t) noexcept { __wrap_free(p); }
