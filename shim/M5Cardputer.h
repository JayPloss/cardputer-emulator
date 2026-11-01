#pragma once
// M5Cardputer as the firmware uses it. The screen is M5GFX's real drawing code on a
// 240x135 canvas; the keyboard is M5Cardputer's real Keyboard_Class on a virtual reader.
#include <Arduino.h>
#include <M5GFX.h>
#include <utility/Keyboard/Keyboard.h>

#include <vector>

namespace m5 {
struct config_t { bool output_power = false; };
enum class board_t { board_M5CardputerADV = 1 };
}

using EmuCanvasBase = m5gfx::M5Canvas;

// Records what is printed, where, so scenarios can read the screen.
class EmuCanvas : public EmuCanvasBase {
 public:
  EmuCanvas();
  explicit EmuCanvas(lgfx::LovyanGFX *parent);
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override { size_t n = 0; while (size--) n += write(*buffer++); return n; }
  using EmuCanvasBase::write;
  template <typename T> void fillScreen(const T &color) { clearText(); EmuCanvasBase::fillScreen(color); }
  void fillScreen() { clearText(); EmuCanvasBase::fillScreen(); }
  void pushSprite(int x, int y);
  void clearText();
  const char *text() const { return text_; }
  void commitText();

 protected:
  char text_[1800] = {0};
  int textLength_ = 0;
  int lastY_ = -1;
  int lastEnd_ = -1;
};
#define M5Canvas EmuCanvas

class EmuDisplay : public EmuCanvas {
 public:
  void setup();
  void setRotation(uint8_t) {}
  void setBrightness(uint8_t) {}
  void present();
};

class EmuSpeaker {
 public:
  bool begin();
  void end();
  void setVolume(uint8_t) {}
  void stop();
  void stop(uint8_t channel);
  size_t isPlaying(uint8_t channel);
  bool tone(float frequency, uint32_t ms, int channel = -1, bool stopCurrent = true);
  bool playRaw(const int16_t *data, size_t samples, uint32_t rate = 44100, bool stereo = false, uint32_t repeat = 1, int channel = -1, bool stopCurrent = false);
};

struct EmuMicConfig { uint32_t sample_rate = 16000; uint32_t magnification = 16; uint32_t noise_filter_level = 0; };
class EmuMic {
 public:
  EmuMicConfig config() { return config_; }
  void config(const EmuMicConfig &value) { config_ = value; }
  bool begin() { return true; }
  void end() {}
  size_t isRecording() { return 0; }
  bool record(int16_t *buffer, size_t samples, uint32_t) { memset(buffer, 0, samples * sizeof(int16_t)); return true; }
 private:
  EmuMicConfig config_;
};

class EmuButton {
 public:
  bool isPressed() const { return pressed_; }
  bool wasClicked() const { return clicked_; }
  void update();
 private:
  bool pressed_ = false;
  bool clicked_ = false;
};

struct EmuPower { int getBatteryVoltage() { return 3986; } };
struct EmuI2C { bool scanID(uint8_t, uint32_t) { return true; } };

class EmuCardputer {
 public:
  void begin(const m5::config_t &config, bool keyboard = true);
  void update();
  EmuDisplay Display;
  EmuSpeaker Speaker;
  EmuMic Mic;
  EmuButton BtnA;
  EmuPower Power;
  EmuI2C In_I2C;
  Keyboard_Class Keyboard;
};
extern EmuCardputer M5Cardputer;

class EmuM5 {
 public:
  m5::config_t config() { return m5::config_t{}; }
  m5::board_t getBoard() { return m5::board_t::board_M5CardputerADV; }
};
extern EmuM5 M5;
