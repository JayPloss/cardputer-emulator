// The Cardputer's screen, speaker, button and virtual keyboard reader.
#include <M5Cardputer.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "device_model.h"
#include "emu.h"

EmuCardputer M5Cardputer;
EmuM5 M5;

// ---- the screen ----

namespace {
constexpr int kWidth = 240;
constexpr int kHeight = 135;
std::mutex screenLock;
uint16_t pixels[kWidth * kHeight];
std::string shownText;
std::atomic<int> clipped{0};
}  // namespace

EmuCanvas::EmuCanvas() = default;
EmuCanvas::EmuCanvas(lgfx::LovyanGFX *parent) : EmuCanvasBase(parent) {}

size_t EmuCanvas::write(uint8_t c) {
  const int x = getCursorX(), y = getCursorY();
  const bool sameLine = y == lastY_;
  if (!sameLine || x > lastEnd_ + 8) {
    if (textLength_ > 0 && textLength_ + 1 < static_cast<int>(sizeof(text_))) text_[textLength_++] = sameLine ? ' ' : '\n';
  }
  if (textLength_ + 1 < static_cast<int>(sizeof(text_))) text_[textLength_++] = c;
  text_[textLength_] = 0;
  const size_t written = EmuCanvasBase::write(c);
  lastY_ = y;
  lastEnd_ = getCursorX();
  if (getCursorX() > width() && c >= 0x20) emu::noteClipped(text_ + (textLength_ > 40 ? textLength_ - 40 : 0), x, y);
  return written;
}
void EmuCanvas::clearText() { textLength_ = 0; text_[0] = 0; lastY_ = lastEnd_ = -1; }
void EmuCanvas::commitText() { emu::screenTextCommit(text_); }
void EmuCanvas::pushSprite(int x, int y) {
  commitText();
  EmuCanvasBase::pushSprite(x, y);
  M5Cardputer.Display.present();
}

void EmuDisplay::setup() {
  emu::HostScope host;  // The real panel's RAM is the panel's, not the heap's.
  setColorDepth(16);
  createSprite(kWidth, kHeight);
  fillScreen(TFT_BLACK);
  present();
}
void EmuDisplay::present() {
  emu::screenCommit(static_cast<const uint16_t *>(getBuffer()));
}

namespace emu {
const uint16_t *screenPixels() { return pixels; }
void screenCommit(const uint16_t *source) {
  std::lock_guard<std::mutex> guard(screenLock);
  memcpy(pixels, source, sizeof(pixels));
}
void screenTextCommit(const char *text) {
  HostScope host;
  std::lock_guard<std::mutex> guard(screenLock);
  shownText = text;
}
void screenText(char *out, size_t size) {
  HostScope host;
  std::lock_guard<std::mutex> guard(screenLock);
  snprintf(out, size, "%s", shownText.c_str());
}
void noteClipped(const char *text, int x, int y) {
  HostScope host;
  static std::mutex lock;
  static std::vector<std::string> seen;
  std::lock_guard<std::mutex> guard(lock);
  std::string line(text);
  const auto newline = line.rfind('\n');
  if (newline != std::string::npos) line = line.substr(newline + 1);
  for (const auto &entry : seen) if (entry == line) return;
  if (seen.size() < 200) seen.push_back(line);
  clipped++;
  printf("[screen] text runs off the right edge at (%d,%d): \"%s\"\n", x, y, line.c_str());
}
int clippedCount() { return clipped; }
bool savePng(const char *path);
}  // namespace emu

// ---- the keyboard ----

namespace {
std::mutex keyLock;
std::vector<Point2D_t> heldKeys;
std::atomic<int> buttonEvents{0};

class VirtualReader : public KeyboardReader {
 public:
  void update() override {
    std::lock_guard<std::mutex> guard(keyLock);
    _key_list = heldKeys;
  }
};
}  // namespace

namespace emu {
void keySet(int x, int y, bool down) {
  HostScope host;
  std::lock_guard<std::mutex> guard(keyLock);
  auto at = std::find_if(heldKeys.begin(), heldKeys.end(), [&](const Point2D_t &key) { return key.x == x && key.y == y; });
  if (down && at == heldKeys.end()) { Point2D_t key; key.x = x; key.y = y; heldKeys.push_back(key); }
  if (!down && at != heldKeys.end()) heldKeys.erase(at);
}
void buttonPress() { buttonEvents++; }
}  // namespace emu

void EmuButton::update() {
  clicked_ = false;
  if (buttonEvents.load() > 0) { buttonEvents--; clicked_ = true; }
}

void EmuCardputer::begin(const m5::config_t &, bool) {
  Display.setup();
  Keyboard.begin(std::unique_ptr<KeyboardReader>(new VirtualReader));
}
void EmuCardputer::update() {
  BtnA.update();
  Keyboard.updateKeyList();
  Keyboard.updateKeysState();
}

// ---- the speaker ----

namespace {
struct Slot { const int16_t *data = nullptr; size_t samples = 0; uint32_t rate = 0; bool stereo = false; uint32_t repeat = 0; uint64_t toneEnd = 0; };
struct Channel { Slot now, next; double position = 0; uint64_t lastEnd = 0; bool session = false; };
constexpr int kChannels = 8;
constexpr int kGapMs = 15;
std::mutex speakerLock;
Channel channels[kChannels];
std::atomic<int> underruns{0};
std::atomic<uint64_t> playedSamples{0};
std::atomic<uint32_t> playedRate{0};
std::atomic<double> level{0};
FILE *wav = nullptr;
uint64_t wavSamples = 0;
uint32_t wavRate = 0;

void openWav() {
  const std::string path = std::string(emu::options.state) + "/audio.wav";
  wav = fopen(path.c_str(), "wb");
  uint8_t header[44] = {0};
  if (wav) fwrite(header, 1, 44, wav);
}
void finishWav() {
  if (!wav) return;
  const uint32_t data = wavSamples * 2, riff = data + 36, fmtSize = 16, rate = wavRate ? wavRate : 22050, byteRate = rate * 2;
  const uint16_t pcm = 1, mono = 1, align = 2, bits = 16;
  fseek(wav, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, wav); fwrite(&riff, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav); fwrite(&fmtSize, 4, 1, wav);
  fwrite(&pcm, 2, 1, wav); fwrite(&mono, 2, 1, wav); fwrite(&rate, 4, 1, wav); fwrite(&byteRate, 4, 1, wav);
  fwrite(&align, 2, 1, wav); fwrite(&bits, 2, 1, wav); fwrite("data", 1, 4, wav); fwrite(&data, 4, 1, wav);
  fflush(wav);
  fseek(wav, 0, SEEK_END);
}

void speakerThread() {
  emu::markHostThread();
  openWav();
  double energy = 0; uint64_t energyCount = 0;
  uint64_t last = emu::micros();
  uint64_t lastFlush = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(5 / emu::options.timeScale));
    const uint64_t now = emu::micros();
    const double elapsed = (now - last) / 1e6;
    last = now;
    std::lock_guard<std::mutex> guard(speakerLock);
    for (int index = 0; index < kChannels; index++) {
      Channel &channel = channels[index];
      if (channel.now.toneEnd && now >= channel.now.toneEnd) channel.now = Slot{};
      if (!channel.now.data) continue;
      double budget = elapsed * channel.now.rate * (channel.now.stereo ? 2 : 1);
      while (budget > 0 && channel.now.data) {
        const size_t position = static_cast<size_t>(channel.position);
        const size_t remaining = channel.now.samples - position;
        const size_t take = std::min<size_t>(remaining, static_cast<size_t>(budget) + 1);
        // Reads the firmware's own buffer as the DMA does: freed too early, ASan objects.
        const int step = channel.now.stereo ? 2 : 1;
        for (size_t i = 0; i < take; i += step) {
          const int16_t sample = channel.now.data[position + i];
          energy += double(sample) * sample; energyCount++;
          if (index == 1 && wav) { fwrite(&sample, 2, 1, wav); wavSamples++; }
        }
        if (index == 1) { playedSamples += take / step; playedRate = channel.now.rate; wavRate = channel.now.rate; }
        channel.position += take;
        budget -= take;
        if (static_cast<size_t>(channel.position) >= channel.now.samples) {
          channel.position = 0;
          if (--channel.now.repeat == 0) { channel.now = channel.next; channel.next = Slot{}; }
          if (!channel.now.data && !channel.now.toneEnd) { channel.lastEnd = now; break; }
        }
      }
    }
    if (energyCount > 4000) { level = std::sqrt(energy / energyCount); energy = 0; energyCount = 0; }
    if (now - lastFlush > 1000000) { lastFlush = now; finishWav(); }
  }
}
void startSpeaker() {
  static std::once_flag started;
  std::call_once(started, [] { std::thread(speakerThread).detach(); });
}
}  // namespace

void EmuDisplay::setBrightness(uint8_t value) { emu::powerSetBrightness(value); }
void EmuSpeaker::setVolume(uint8_t value) { emu::powerSetVolume(value); }
bool EmuSpeaker::begin() { emu::HostScope host; startSpeaker(); return true; }
void EmuSpeaker::end() { stop(); }
void EmuSpeaker::stop() { for (int i = 0; i < kChannels; i++) stop(i); }
void EmuSpeaker::stop(uint8_t index) {
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(speakerLock);
  channels[index].now = Slot{}; channels[index].next = Slot{};
  channels[index].position = 0; channels[index].session = false; channels[index].lastEnd = 0;
}
size_t EmuSpeaker::isPlaying(uint8_t index) {
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(speakerLock);
  const Channel &channel = channels[index];
  if (channel.now.data || channel.now.toneEnd) return channel.next.data ? 2 : 1;
  return 0;
}
bool EmuSpeaker::tone(float, uint32_t ms, int channel, bool) {
  emu::HostScope host;
  startSpeaker();
  std::lock_guard<std::mutex> guard(speakerLock);
  const int index = channel < 0 ? 0 : channel;
  channels[index].now = Slot{}; channels[index].next = Slot{};
  channels[index].now.toneEnd = emu::micros() + uint64_t(ms) * 1000;
  return true;
}
bool EmuSpeaker::playRaw(const int16_t *data, size_t samples, uint32_t rate, bool stereo, uint32_t repeat, int channel, bool stopCurrent) {
  emu::HostScope host;
  startSpeaker();
  std::lock_guard<std::mutex> guard(speakerLock);
  const int index = channel < 0 ? 0 : channel;
  Channel &target = channels[index];
  Slot slot; slot.data = data; slot.samples = samples; slot.rate = rate; slot.stereo = stereo; slot.repeat = repeat;
  if (stopCurrent) { target.now = slot; target.next = Slot{}; target.position = 0; return true; }
  if (!target.now.data) {
    const uint64_t now = emu::micros();
    if (index == 1 && target.session && target.lastEnd && now - target.lastEnd > kGapMs * 1000) {
      underruns++;
      printf("[audio] underrun: the speaker ran dry for %llu ms at %.1f s\n", static_cast<unsigned long long>((now - target.lastEnd) / 1000), emu::millis() / 1000.0);
    }
    target.session = true;
    target.now = slot;
    target.position = 0;
    return true;
  }
  if (target.next.data) return false;
  target.next = slot;
  return true;
}

namespace emu {
double audioSeconds() { const uint32_t rate = playedRate; return rate ? double(playedSamples) / rate : 0; }
int audioUnderruns() { return underruns; }
double audioLevel() { return level; }
}  // namespace emu
