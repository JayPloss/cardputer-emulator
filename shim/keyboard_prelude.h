#pragma once
// Stands in for the hardware readers named by M5Cardputer's Keyboard.cpp, built here
// with those includes removed. Only the virtual reader is ever installed.
#include <Arduino.h>
#include <memory>
#include <utility/Keyboard/KeyboardReader/KeyboardReader.h>
namespace m5 { enum class board_t { board_M5Cardputer, board_M5CardputerADV }; }
struct IOMatrixKeyboardReader : KeyboardReader {};
struct TCA8418KeyboardReader : KeyboardReader {};
static struct { m5::board_t getBoard() { return m5::board_t::board_M5CardputerADV; } } M5;
