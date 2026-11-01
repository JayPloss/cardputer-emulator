#pragma once
// NVS, backed by a file in the run's state folder.
#include <Arduino.h>
class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false);
  void end() {}
  String getString(const char *key, const String &fallback = String());
  size_t putString(const char *key, const String &value);
  uint8_t getUChar(const char *key, uint8_t fallback = 0);
  size_t putUChar(const char *key, uint8_t value);
  int32_t getInt(const char *key, int32_t fallback = 0);
  size_t putInt(const char *key, int32_t value);
  bool getBool(const char *key, bool fallback = false);
  size_t putBool(const char *key, bool value);
};
