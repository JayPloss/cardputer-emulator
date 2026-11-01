#pragma once
// Just enough for M5Cardputer's own Keyboard.cpp to compile; its begin() that picks a
// hardware reader is never called.
#include <Arduino.h>
namespace m5 { namespace board_t_shim {} }
