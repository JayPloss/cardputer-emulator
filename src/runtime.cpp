// The virtual clock, firmware tasks, the core 0 watchdog and crash reports.
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
#include <cxxabi.h>
#include <vector>

#include "device_model.h"
#include "emu.h"

namespace emu {
Options options;
bool wifiAvailable = true;
char **savedArgv = nullptr;
const char *expectedCrash = nullptr;

namespace {
using Clock = std::chrono::steady_clock;
const Clock::time_point started = Clock::now();

constexpr size_t kHostStack = 4 * 1024 * 1024;
constexpr uint8_t kPaint = 0xA5;

struct Task {
  std::string name;
  void (*fn)(void *) = nullptr;
  void *arg = nullptr;
  uint32_t stackBytes = 0;
  int priority = 0;
  int core = 0;
  uint8_t *stack = nullptr;
  void *heapBlock = nullptr;
  std::atomic<int> blocked{0};
  pthread_t thread{};
};
std::vector<Task *> tasks;
std::mutex taskLock;
thread_local Task *currentTask = nullptr;
std::atomic<uint32_t> lastIdle[2];
std::atomic<bool> crashing{false};

size_t highWater(const Task &task) {
  // The stack grows down: the paint left at the bottom is what was never touched.
  size_t untouched = 0;
  while (untouched < kHostStack && reinterpret_cast<volatile uint8_t *>(task.stack)[untouched] == kPaint) untouched++;
  return kHostStack - untouched;
}

void *taskMain(void *raw) {
  auto *task = static_cast<Task *>(raw);
  currentTask = task;
  setHeapOwner(task->name.c_str());
  // Paint everything below this frame; the high-water mark is measured from here.
  volatile uint8_t marker = 0;
  uint8_t *here = const_cast<uint8_t *>(&marker) - 256;
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
  for (uint8_t *at = task->stack + 4096; at < here; at++) *at = kPaint;
#endif
  task->fn(task->arg);
  return nullptr;
}

void watchdogThread() {
  markHostThread();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const uint32_t now = millis();
    std::string running;
    bool idle = true;
    {
      std::lock_guard<std::mutex> guard(taskLock);
      for (Task *task : tasks) {
        if (task->core == 0 && task->blocked.load() == 0) { idle = false; running += " " + task->name; }
      }
    }
    if (idle) { lastIdle[0] = now; continue; }
    if (now - lastIdle[0].load() > model::kWatchdogMs) {
      std::string detail = "task watchdog: IDLE0 starved for 5 s; running on core 0:" + running;
      crash("task watchdog", detail.c_str());
    }
  }
}

void symbolizedBacktrace() {
  void *frames[48];
  const int count = backtrace(frames, 48);
  char **names = backtrace_symbols(frames, count);
  for (int i = 0; i < count; i++) {
    const char *open = names ? strchr(names[i], '(') : nullptr;
    const char *plus = open ? strchr(open, '+') : nullptr;
    std::string name = open && plus ? std::string(open + 1, plus - open - 1) : "";
    int status = 0;
    char *pretty = name.empty() ? nullptr : abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    const std::string shown = pretty ? pretty : (name.empty() ? (names ? names[i] : "?") : name);
    if (shown.find("emu::crash") == std::string::npos && shown.find("symbolizedBacktrace") == std::string::npos) printf("  %2d %s\n", i, shown.c_str());
  }
}

void onTerminate() {
  std::string detail = "uncaught C++ exception";
  if (auto *type = abi::__cxa_current_exception_type()) {
    int status = 0;
    char *pretty = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
    detail += std::string(": ") + (pretty ? pretty : type->name());
  }
  crash("abort()", detail.c_str());
}

void onSignal(int number) {
  std::string detail = std::string("signal ") + strsignal(number);
  crash("panic", detail.c_str());
}
}  // namespace

uint64_t micros() {
  const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
  return static_cast<uint64_t>(elapsed * options.timeScale);
}
uint32_t millis() { return static_cast<uint32_t>(micros() / 1000); }

static void realSleepUs(uint64_t virtualUs) {
  const auto real = std::chrono::duration<double, std::micro>(virtualUs / options.timeScale);
  std::this_thread::sleep_for(real);
}

Blocked::Blocked() { if (currentTask) currentTask->blocked++; }
Blocked::~Blocked() { if (currentTask) currentTask->blocked--; }

void sleepMs(uint32_t ms) { Blocked blocked; realSleepUs(uint64_t(ms) * 1000); }
void chargeUs(uint32_t us) { realSleepUs(us); }
void chargeMs(uint32_t ms) { realSleepUs(uint64_t(ms) * 1000); }

void deleteCurrentTask() {
  Task *task = currentTask;
  { std::lock_guard<std::mutex> guard(taskLock); tasks.erase(std::find(tasks.begin(), tasks.end(), task)); }
  HostScope host;
  // On FreeRTOS the idle task frees it a moment later.
  deviceFree(task->heapBlock);
  pthread_exit(nullptr);
}

void *createTask(void (*fn)(void *), const char *name, uint32_t stackBytes, void *arg, int priority, int core, void *heapBlock) {
  HostScope host;
  auto *task = new Task;
  task->heapBlock = heapBlock; task->name = name; task->fn = fn; task->arg = arg; task->stackBytes = stackBytes; task->priority = priority; task->core = core;
  task->stack = static_cast<uint8_t *>(mmap(nullptr, kHostStack, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  memset(task->stack, kPaint, 4096);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, task->stack, kHostStack);
  { std::lock_guard<std::mutex> guard(taskLock); tasks.push_back(task); }
  pthread_create(&task->thread, &attr, taskMain, task);
  pthread_detach(task->thread);
  pthread_attr_destroy(&attr);
  static std::once_flag watchdogStarted;
  std::call_once(watchdogStarted, [] { lastIdle[0] = millis(); std::thread(watchdogThread).detach(); });
  return task;
}

bool inHostScope();
void *currentTaskHandle() { return inHostScope() ? nullptr : currentTask; }

void stackReport() {
  std::lock_guard<std::mutex> guard(taskLock);
  printf("[stack] task, high water on this host, configured on the device:\n");
  for (Task *task : tasks) {
    const size_t water = highWater(*task);
    const bool over = water * 100 > size_t(task->stackBytes) * model::kHostStackFactorPercent;
    printf("[stack]   %-14s %6zu  %6u%s\n", task->name.c_str(), water, task->stackBytes, over ? "  OVER" : "");
  }
  fflush(stdout);
}

void crash(const char *kind, const char *detail) {
  bool expected = false;
  if (crashing.exchange(true)) { for (;;) pause(); }
  markHostThread();
  const char *taskName = currentTask ? currentTask->name.c_str() : "(host)";
  printf("\n===== CRASH: the device would restart here =====\n");
  printf("%s on task '%s' at %.3f s: %s\n", kind, taskName, millis() / 1000.0, detail);
  heapReport("at crash");
  printf("backtrace:\n");
  symbolizedBacktrace();
  stackReport();
  printf("================================================\n");
  fflush(stdout);
  if (expectedCrash && (strstr(detail, expectedCrash) || strstr(kind, expectedCrash))) expected = true;
  if (expected) { printf("[scenario] crash was expected: PASS\n"); fflush(stdout); _exit(0); }
  _exit(3);
}

void installCrashHandlers() {
  std::set_terminate(onTerminate);
  for (int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) signal(number, onSignal);
}

}  // namespace emu

extern "C" void __wrap_abort() { emu::crash("abort()", "abort called"); }
