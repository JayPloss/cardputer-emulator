// Runs the real firmware's setup() and loop() on a task sized like Arduino's loopTask.
#include <Arduino.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "device_model.h"
#include "emu.h"

void setup();
void loop();

namespace emu { void installCrashHandlers(); }

#ifndef EMU_HEAP_START
#define EMU_HEAP_START 0
#endif
extern "C" uint32_t emuHeapStart() { return EMU_HEAP_START ? EMU_HEAP_START : model::kDefaultHeapStart; }

static void loopTask(void *) {
  setup();
  for (;;) loop();
}

// One line every 5 s: the free-heap timeline from boot, for the report and for graphs.
static void timelineThread() {
  emu::markHostThread();
  char path[512];
  snprintf(path, sizeof(path), "%s/timeline.csv", emu::options.state);
  FILE *file = fopen(path, "a");
  if (!file) return;
  fprintf(file, "seconds,free,largest,lowest,owners\n");
  for (;;) {
    char owners[400] = "";
    emu::heapOwners(owners, sizeof(owners));
    fprintf(file, "%u,%zu,%zu,%zu,%s\n", emu::millis() / 1000, emu::heapFree(), emu::heapLargest(), emu::heapLowest(), owners);
    fflush(file);
    std::this_thread::sleep_for(std::chrono::duration<double>(5 / emu::options.timeScale));
  }
}

static void holdSystemBlocks() {
  size_t total = 0;
  for (const auto &block : model::kSystemBlocks) {
    emu::deviceAlloc(block.bytes, block.what);
    total += block.bytes;
  }
  printf("[heap] device heap %zu bytes, system holds %zu before setup()\n", emu::heapTotal(), total);
}

int main(int argc, char **argv) {
  emu::markHostThread();
  emu::savedArgv = argv;
  for (int i = 1; i < argc; i++) {
    const auto value = [&] { return i + 1 < argc ? argv[++i] : ""; };
    if (!strcmp(argv[i], "--card")) emu::options.card = value();
    else if (!strcmp(argv[i], "--state")) emu::options.state = value();
    else if (!strcmp(argv[i], "--scenario")) emu::options.scenario = value();
    else if (!strcmp(argv[i], "--time-scale")) emu::options.timeScale = atof(value());
    else if (!strcmp(argv[i], "--resume")) emu::options.resumeLine = atoi(value());
    else if (!strcmp(argv[i], "--window")) emu::options.window = true;
    else { fprintf(stderr, "usage: %s --card DIR --state DIR [--scenario FILE] [--time-scale N] [--window]\n", argv[0]); return 2; }
  }
  if (!emu::options.card || !emu::options.state) { fprintf(stderr, "--card and --state are required\n"); return 2; }
  setvbuf(stdout, nullptr, _IOLBF, 0);
  emu::installCrashHandlers();
  holdSystemBlocks();
  std::thread(timelineThread).detach();
  emu::createTask(loopTask, "loopTask", CONFIG_ARDUINO_LOOP_STACK_SIZE, nullptr, 1, 1);
  if (emu::options.scenario) {
    const int result = emu::runScenario();
    fflush(stdout);
    _exit(result);
  }
  emu::runWindow();
  _exit(0);
}
