#pragma once
#ifndef Arduino_h
#define Arduino_h
#define HAL_ESP32_HAL_H_
#define __ARDUHAL_LOG_H__
// The Arduino surface the firmware and its libraries use, running on the emulator's
// runtime. String, Print and Stream are the real ones from arduino-esp32's core.
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __cplusplus
#include <algorithm>
#include <cmath>

#include "esp_log.h"
#include "emu_config.h"
#include "freertos_shim.h"
#include <pgmspace.h>
#include <WString.h>
#include <Print.h>
#include <Stream.h>
#include <stdlib_noniso.h>
#include <Printable.h>
#include <IPAddress.h>

#include <sys/time.h>
#define settimeofday(tv, tz) (0)
typedef enum { GPIO_NUM_12 = 12, GPIO_NUM_14 = 14, GPIO_NUM_39 = 39, GPIO_NUM_40 = 40 } gpio_num_t;
using std::min;
using std::max;
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define HIGH 1
#define LOW 0
#define SDA 8
#define SCL 9
#define IRAM_ATTR
#define ARDUINO_ISR_ATTR
#define PROGMEM_LATE
typedef bool boolean;
typedef uint8_t byte;

unsigned long millis();
unsigned long micros();
void delay(uint32_t ms);
void yield();
long random(long max);
long random(long low, long high);
bool setCpuFrequencyMhz(uint32_t mhz);
void configTzTime(const char *tz, const char *a, const char *b = nullptr, const char *c = nullptr);
bool getLocalTime(struct tm *info, uint32_t ms = 5000);
void setup();
void loop();

class SerialClass : public Print {
 public:
  void begin(unsigned long) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  operator bool() const { return true; }
};
extern SerialClass Serial;

class EspClass {
 public:
  uint32_t getFreeHeap();
  uint32_t getMaxAllocHeap();
  uint32_t getHeapSize();
  uint32_t getMinFreeHeap();
  [[noreturn]] void restart();
};
extern EspClass ESP;

extern "C" uint32_t esp_random();
extern "C" void esp_fill_random(void *buffer, size_t length);
#endif  // __cplusplus
#endif  // Arduino_h
