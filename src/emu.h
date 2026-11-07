#pragma once
#include <ctime>

#include <stddef.h>
#include <stdint.h>

// The emulator's runtime: the virtual clock, the device heap, tasks and crash reports.
namespace emu {

uint32_t millis();
uint64_t micros();
void sleepMs(uint32_t ms);       // Blocked: IDLE can run on this task's core.
void chargeUs(uint32_t us);      // Busy: the CPU is doing work; the watchdog sees it.
void chargeMs(uint32_t ms);

// Marks the calling firmware task as blocked for a scope (socket waits and the like).
struct Blocked { Blocked(); ~Blocked(); };

// Host code on a firmware thread (shim bookkeeping) stays off the device heap.
struct HostScope { HostScope(); ~HostScope(); };
void markHostThread();

// A block the system holds on the firmware's behalf: counted like any other.
void *deviceAlloc(size_t bytes, const char *tag);
void deviceFree(void *block);

size_t heapFree();
size_t heapLargest();
size_t heapLowest();
size_t heapTotal();
void heapReport(const char *when);
// Who a task's allocations are charged to, for the per-owner report.
void setHeapOwner(const char *name);
void heapOwners(char *out, size_t size);

void *createTask(void (*fn)(void *), const char *name, uint32_t stackBytes, void *arg, int priority, int core, void *heapBlock = nullptr);
[[noreturn]] void deleteCurrentTask();
void stackReport();
void *currentTaskHandle();  // Null on host threads and in host scopes: they own nothing.

// Prints a crash report in the shape of the device's core dump and ends the run.
[[noreturn]] void crash(const char *kind, const char *detail);

// Options set from the command line.
struct Options {
  const char *card = nullptr;
  const char *state = nullptr;
  const char *scenario = nullptr;
  double timeScale = 1;
  int resumeLine = 0;
  bool window = false;
};
extern Options options;
extern bool wifiAvailable;
// The wall clock, on the virtual clock; a scenario's `clock` line sets it.
void setWallClock(time_t epoch);
time_t wallClock();
// Whether a time server would answer: joined to a network with the internet.
bool ntpReachable();
// Tones the firmware started, since the last count was taken.
int takeToneCount();
// The Wi-Fi networks in range. Behaviour says what a joined network lets through:
// home (everything), nonav (internet, but the music server's address goes nowhere), captive
// (joins, TLS fails at once), nointernet (joins, everything times out), joinfail (never joins).
void netAdd(const char *ssid, const char *password, const char *behaviour);
void netClear();
// Card faults, for scenarios: a power cut at a chosen file operation restarts the run from
// the next scenario line, with the card as it stood. A full card fails every write.
void armPowerCut(const char *when, const char *what, const char *resumeLine);
void cardFull(bool on);
void bypassFree(bool on);
extern char **savedArgv;

// The screen, for scenarios and the window.
const uint16_t *screenPixels();
void screenCommit(const uint16_t *pixels);
void screenTextCommit(const char *text);
void screenText(char *out, size_t size);
bool savePng(const char *path);
void noteClipped(const char *text, int x, int y);
int clippedCount();

// Keyboard input from scenarios and the window: physical (column, row) positions.
void keySet(int x, int y, bool down);
void buttonPress();

// The speaker, for scenarios.
double audioSeconds();
int audioUnderruns();
double audioLevel();

// Battery prediction, once a fitted model is loaded (src/power.cpp).
void powerLoad(const char *path, double startLevel);
void powerSetBrightness(int value);
void powerSetVolume(int value);
void powerStreaming(int delta);
void powerHandshake(int delta);
void powerReport();
bool powerOk();
bool powerLoaded();

int runScenario();  // Returns on completion: 0 pass, 1 fail.
void runWindow();

}  // namespace emu

extern "C" uint32_t emuHeapStart();
