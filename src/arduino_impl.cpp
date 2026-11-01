// Arduino, FreeRTOS, NVS and the SD card, on the emulator's runtime.
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <string>

#include "device_model.h"
#include "emu.h"

SerialClass Serial;
EspClass ESP;
SPIClass SPI;
SDClass SD;

unsigned long millis() { return emu::millis(); }
unsigned long micros() { return emu::micros(); }
void delay(uint32_t ms) { emu::sleepMs(ms); }
void yield() { emu::sleepMs(1); }
long random(long max) { return max > 0 ? static_cast<long>(esp_random() % static_cast<uint32_t>(max)) : 0; }
long random(long low, long high) { return low + random(high - low); }
bool setCpuFrequencyMhz(uint32_t) { return true; }
void configTzTime(const char *tz, const char *, const char *, const char *) { setenv("TZ", tz, 1); tzset(); }
bool getLocalTime(struct tm *info, uint32_t) {
  const time_t now = time(nullptr);
  localtime_r(&now, info);
  return true;
}

size_t SerialClass::write(uint8_t c) { return write(&c, 1); }
size_t SerialClass::write(const uint8_t *buffer, size_t size) {
  fwrite(buffer, 1, size, stdout);
  fflush(stdout);
  return size;
}

uint32_t EspClass::getFreeHeap() { return emu::heapFree(); }
uint32_t EspClass::getMaxAllocHeap() { return emu::heapLargest(); }
uint32_t EspClass::getHeapSize() { return emu::heapTotal(); }
uint32_t EspClass::getMinFreeHeap() { return emu::heapLowest(); }
void EspClass::restart() { emu::crash("restart", "ESP.restart() called"); }

extern "C" uint32_t esp_random() {
  emu::HostScope host;
  static std::mt19937 generator(12345);
  static std::mutex lock;
  std::lock_guard<std::mutex> guard(lock);
  return generator();
}
extern "C" void esp_fill_random(void *buffer, size_t length) {
  auto *bytes = static_cast<uint8_t *>(buffer);
  for (size_t i = 0; i < length; i++) bytes[i] = esp_random();
}

// ---- FreeRTOS ----

extern "C" void vTaskDelay(TickType_t ticks) { emu::sleepMs(ticks); }
extern "C" void vTaskDelete(TaskHandle_t) { emu::deleteCurrentTask(); }
extern "C" void *xTaskGetCurrentTaskHandle(void) { return emu::currentTaskHandle(); }

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, unsigned priority, TaskHandle_t *handle, int core) {
  // The device takes the stack and the task control block from the heap.
  // Taken from the heap as the device does, outside any app's budget.
  void *block = emu::deviceAlloc(stack + 360, name);
  if (!block) { printf("[task] no RAM for task '%s' (%u bytes)\n", name, stack); return pdFALSE; }
  void *task = emu::createTask(fn, name, stack, arg, priority, core, block);
  if (handle) *handle = task;
  return pdPASS;
}

namespace {
struct EmuMutex { std::timed_mutex lock; std::atomic<void *> owner{nullptr}; };
thread_local char threadIdentity;
}
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex() {
  emu::HostScope host;
  return new EmuMutex;
}
extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
  auto *m = static_cast<EmuMutex *>(mutex);
  bool got = m->lock.try_lock();
  if (!got) {
    emu::Blocked blocked;
    if (ticks == portMAX_DELAY) { m->lock.lock(); got = true; }
    else if (ticks) got = m->lock.try_lock_for(std::chrono::duration<double, std::milli>(ticks / emu::options.timeScale));
  }
  if (got) m->owner = &threadIdentity;
  return got ? pdTRUE : pdFALSE;
}
extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
  // As on FreeRTOS, a task that doesn't hold the mutex can't give it back.
  auto *m = static_cast<EmuMutex *>(mutex);
  if (m->owner.load() != &threadIdentity) return pdFALSE;
  m->owner = nullptr;
  m->lock.unlock();
  return pdTRUE;
}
extern "C" void *heap_caps_malloc(size_t bytes, uint32_t) { return malloc(bytes); }

// ---- NVS ----

namespace {
std::mutex nvsLock;
std::map<std::string, std::string> &nvs() {
  static auto *values = [] {
    auto *loaded = new std::map<std::string, std::string>;
    std::ifstream file(std::string(emu::options.state) + "/nvs.txt");
    std::string line;
    while (std::getline(file, line)) {
      const auto tab = line.find('\t');
      if (tab != std::string::npos) (*loaded)[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return loaded;
  }();
  return *values;
}
void nvsPut(const char *key, const std::string &value) {
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(nvsLock);
  nvs()[key] = value;
  std::ofstream file(std::string(emu::options.state) + "/nvs.txt");
  for (const auto &[k, v] : nvs()) file << k << '\t' << v << '\n';
}
bool nvsGet(const char *key, std::string &value) {
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(nvsLock);
  const auto found = nvs().find(key);
  if (found == nvs().end()) return false;
  value = found->second;
  return true;
}
long nvsNumber(const char *key, long fallback) {
  emu::HostScope host;
  std::string value;
  return nvsGet(key, value) ? atol(value.c_str()) : fallback;
}
}  // namespace

bool Preferences::begin(const char *, bool) { return true; }
String Preferences::getString(const char *key, const String &fallback) {
  char copy[256];
  {
    emu::HostScope host;
    std::string value;
    if (!nvsGet(key, value)) return fallback;
    snprintf(copy, sizeof(copy), "%s", value.c_str());
  }
  return String(copy);
}
size_t Preferences::putString(const char *key, const String &value) {
  std::string copy;
  { emu::HostScope host; copy = value.c_str(); nvsPut(key, copy); }
  return value.length();
}
uint8_t Preferences::getUChar(const char *key, uint8_t fallback) { return nvsNumber(key, fallback); }
size_t Preferences::putUChar(const char *key, uint8_t value) { emu::HostScope host; nvsPut(key, std::to_string(value)); return 1; }
int32_t Preferences::getInt(const char *key, int32_t fallback) { return nvsNumber(key, fallback); }
size_t Preferences::putInt(const char *key, int32_t value) { emu::HostScope host; nvsPut(key, std::to_string(value)); return 4; }
bool Preferences::getBool(const char *key, bool fallback) { return nvsNumber(key, fallback) != 0; }
size_t Preferences::putBool(const char *key, bool value) { emu::HostScope host; nvsPut(key, value ? "1" : "0"); return 1; }

// ---- SD ----

namespace {
std::string cutWhen, cutWhat, cutResume;
std::atomic<bool> cutArmed{false}, cardIsFull{false};
void maybeCut(const char *when, const std::string &path, FILE *flushFirst = nullptr) {
  if (!cutArmed.load()) return;
  emu::HostScope host;
  if (cutWhen != when || path.find(cutWhat) == std::string::npos) return;
  cutArmed = false;
  printf("[card] power cut at %s of %s\n", when, path.c_str());
  if (flushFirst) fflush(flushFirst);
  std::vector<char *> args;
  for (char **arg = emu::savedArgv; *arg; arg++) {
    if (!strcmp(*arg, "--resume")) { arg++; continue; }
    args.push_back(*arg);
  }
  static char flag[] = "--resume";
  args.push_back(flag);
  args.push_back(const_cast<char *>(cutResume.c_str()));
  args.push_back(nullptr);
  fflush(stdout);
  execv("/proc/self/exe", args.data());
}
}  // namespace

namespace emu {
void armPowerCut(const char *when, const char *what, const char *resumeLine) {
  HostScope host;
  cutWhen = when; cutWhat = what; cutResume = resumeLine;
  cutArmed = true;
}
void cardFull(bool on) { cardIsFull = on; }
}  // namespace emu

struct File::State {
  int refs = 1;
  FILE *file = nullptr;
  DIR *dir = nullptr;
  std::string hostPath;
  std::string name;
  std::string cardPath;
  void *block = nullptr;  // What FATFS and stdio hold on the device.
  uint64_t debtUs = 0;
};

namespace {
std::string hostPath(const char *cardPath) { return std::string(emu::options.card) + "/" + (cardPath[0] == '/' ? cardPath + 1 : cardPath); }
void charge(File::State *state, size_t bytes) {
  state->debtUs += uint64_t(bytes) * 1000000 / model::kSdBytesPerSecond;
  if (state->debtUs >= 1000) { emu::chargeUs(state->debtUs); state->debtUs = 0; }
}
}  // namespace

File::File(const File &other) : Stream(), state_(other.state_) { if (state_) state_->refs++; }
File &File::operator=(const File &other) {
  if (this == &other) return *this;
  close();
  state_ = other.state_;
  if (state_) state_->refs++;
  return *this;
}
File::~File() { close(); }
void File::close() {
  if (!state_) return;
  emu::HostScope host;
  if (--state_->refs == 0) {
    if (state_->file) fclose(state_->file);
    if (state_->dir) closedir(state_->dir);
    emu::deviceFree(state_->block);  // The direct route, as libc's own free takes.
    delete state_;
  }
  state_ = nullptr;
}
int File::available() {
  if (!state_ || !state_->file) return 0;
  emu::HostScope host;
  const long here = ftell(state_->file);
  fseek(state_->file, 0, SEEK_END);
  const long end = ftell(state_->file);
  fseek(state_->file, here, SEEK_SET);
  return end > here ? end - here : 0;
}
int File::read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
int File::peek() {
  if (!state_ || !state_->file) return -1;
  emu::HostScope host;
  const int c = fgetc(state_->file);
  if (c != EOF) ungetc(c, state_->file);
  return c;
}
size_t File::read(uint8_t *buffer, size_t size) {
  if (!state_ || !state_->file) return 0;
  emu::HostScope host;
  const size_t got = fread(buffer, 1, size, state_->file);
  charge(state_, got);
  return got;
}
size_t File::write(uint8_t c) { return write(&c, 1); }
size_t File::write(const uint8_t *buffer, size_t size) {
  if (!state_ || !state_->file) return 0;
  emu::HostScope host;
  if (cardIsFull.load()) return 0;
  // A power cut lands part way through this write, with the part before it on the card.
  if (cutArmed.load() && cutWhen == "write" && state_->hostPath.find(cutWhat) != std::string::npos) {
    fwrite(buffer, 1, size / 2, state_->file);
    maybeCut("write", state_->hostPath, state_->file);
  }
  const size_t wrote = fwrite(buffer, 1, size, state_->file);
  charge(state_, wrote * 2);
  return wrote;
}
void File::flush() { if (state_ && state_->file) { emu::HostScope host; fflush(state_->file); } }
bool File::seek(uint32_t position) { if (!state_ || !state_->file) return false; emu::HostScope host; return fseek(state_->file, position, SEEK_SET) == 0; }
size_t File::size() {
  if (!state_) return 0;
  emu::HostScope host;
  if (state_->file) fflush(state_->file);
  struct stat info{};
  stat(state_->hostPath.c_str(), &info);
  return info.st_size;
}
size_t File::position() { if (!state_ || !state_->file) return 0; emu::HostScope host; return ftell(state_->file); }
bool File::isDirectory() { return state_ && state_->dir; }
const char *File::name() { return state_ ? state_->name.c_str() : ""; }
const char *File::path() { return state_ ? state_->cardPath.c_str() : ""; }
File File::openNextFile() {
  if (!state_ || !state_->dir) return File();
  std::string child;
  {
    emu::HostScope host;
    while (dirent *entry = readdir(state_->dir)) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      child = state_->cardPath + "/" + entry->d_name;
      break;
    }
  }
  if (child.empty()) return File();
  return SD.open(child.c_str(), FILE_READ);
}

bool SDClass::begin(int, SPIClass &, uint32_t) {
  struct stat info{};
  return stat(emu::options.card, &info) == 0;
}
File SDClass::open(const char *path, const char *mode) {
  // On the device, libc allocates an open file's memory through malloc, and its free goes straight
  // to the heap, past any --wrap (a real boot loop came from that). So: in by malloc, out directly.
  void *block = malloc(model::kOpenFileBytes);
  if (!block) return File();  // FATFS reports out of memory; the caller sees a closed file.
  File result;
  emu::HostScope host;
  auto *state = new File::State;
  state->block = block;
  state->hostPath = hostPath(path);
  state->cardPath = path;
  if (state->cardPath.size() > 1 && state->cardPath.back() == '/') state->cardPath.pop_back();
  state->name = state->cardPath.substr(state->cardPath.rfind('/') + 1);
  struct stat info{};
  const bool exists = stat(state->hostPath.c_str(), &info) == 0;
  if (exists && S_ISDIR(info.st_mode)) state->dir = opendir(state->hostPath.c_str());
  else {
    if (mode[0] != 'r') {
      const auto slash = state->hostPath.rfind('/');
      struct stat parent{};
      (void)slash; (void)parent;
    }
    state->file = fopen(state->hostPath.c_str(), mode);
  }
  if (!state->file && !state->dir) { emu::deviceFree(block); delete state; return File(); }
  result.state_ = state;
  return result;
}
bool SDClass::exists(const char *path) { emu::HostScope host; struct stat info{}; return stat(hostPath(path).c_str(), &info) == 0; }
bool SDClass::remove(const char *path) { emu::HostScope host; maybeCut("remove", path); return ::remove(hostPath(path).c_str()) == 0; }
bool SDClass::rename(const char *from, const char *to) {
  emu::HostScope host;
  maybeCut("rename", from);
  // FAT refuses to rename onto an existing name.
  struct stat info{};
  if (stat(hostPath(to).c_str(), &info) == 0) return false;
  return ::rename(hostPath(from).c_str(), hostPath(to).c_str()) == 0;
}

// ---- MD5, for Subsonic's login token ----

extern "C" int mbedtls_md5_ret(const unsigned char *input, size_t length, unsigned char output[16]) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
      0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
      0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
      0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
      0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
      0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const uint8_t S[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  const size_t blocks = (length + 8) / 64 + 1;
  uint8_t chunk[64];
  for (size_t b = 0; b < blocks; b++) {
    for (size_t i = 0; i < 64; i++) {
      const size_t at = b * 64 + i;
      chunk[i] = at < length ? input[at] : at == length ? 0x80 : 0;
    }
    if (b == blocks - 1) { const uint64_t bits = uint64_t(length) * 8; memcpy(chunk + 56, &bits, 8); }
    uint32_t a = h[0], c = h[2], d = h[3], bb = h[1];
    for (int i = 0; i < 64; i++) {
      uint32_t f; int g;
      if (i < 16) { f = (bb & c) | (~bb & d); g = i; }
      else if (i < 32) { f = (d & bb) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = bb ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (bb | ~d); g = (7 * i) % 16; }
      uint32_t word; memcpy(&word, chunk + g * 4, 4);
      const uint32_t rotate = S[(i / 16) * 4 + i % 4];
      const uint32_t sum = a + f + K[i] + word;
      const uint32_t temp = d; d = c; c = bb; bb = bb + ((sum << rotate) | (sum >> (32 - rotate))); a = temp;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d;
  }
  memcpy(output, h, 16);
  return 0;
}
