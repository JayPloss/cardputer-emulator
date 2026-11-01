#pragma once
#include <Arduino.h>
class SPIClass {
 public:
  void begin(int = -1, int = -1, int = -1, int = -1) {}
};
extern SPIClass SPI;
