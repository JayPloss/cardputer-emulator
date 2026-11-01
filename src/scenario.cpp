// Scripted runs: press keys, wait, check the screen and memory, save pictures.
// The commands are in README.md.
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "emu.h"

namespace emu {
extern const char *expectedCrash;
namespace {
constexpr int kKeyHoldMs = 120;
struct Key { int x, y; };

const std::map<std::string, Key> &namedKeys() {
  static const std::map<std::string, Key> keys = {
      {"esc", {0, 0}}, {"del", {13, 0}}, {"tab", {0, 1}}, {"fn", {0, 2}}, {"shift", {1, 2}}, {"enter", {13, 2}},
      {"ctrl", {0, 3}}, {"opt", {1, 3}}, {"alt", {2, 3}}, {"space", {13, 3}},
      {"up", {11, 2}}, {"left", {10, 3}}, {"down", {11, 3}}, {"right", {12, 3}},
  };
  return keys;
}
const char *const kRows[4] = {"`1234567890-=", "\tqwertyuiop[]\\", "  asdfghjkl;'", "   zxcvbnm,./"};

bool findCharacter(char c, Key &key) {
  for (int y = 0; y < 4; y++) {
    const char *row = kRows[y];
    for (int x = 0; row[x]; x++) if (row[x] == c && c != ' ') { key = {x, y}; return true; }
  }
  return false;
}
void pressKey(const Key &key, int ms = kKeyHoldMs) {
  keySet(key.x, key.y, true);
  sleepMs(ms);
  keySet(key.x, key.y, false);
  sleepMs(ms);
}
std::string shown() { char text[2000]; screenText(text, sizeof(text)); return text; }
std::string unquote(const std::string &value) {
  std::string out = value;
  if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
  return out;
}
int lineNumber = 0;
int fail(const std::string &why) {
  printf("[scenario] line %d FAILED: %s\n", lineNumber, why.c_str());
  printf("[scenario] screen was:\n%s\n", shown().c_str());
  char path[512];
  snprintf(path, sizeof(path), "%s/failure.png", options.state);
  savePng(path);
  heapReport("at failure");
  stackReport();
  return 1;
}
}  // namespace

int runScenario() {
  std::ifstream file(options.scenario);
  if (!file) { printf("[scenario] cannot read %s\n", options.scenario); return 1; }
  std::vector<std::string> lines;
  std::vector<std::string> block;
  int repeat = 0;
  for (std::string line; std::getline(file, line);) {
    if (line.rfind("repeat ", 0) == 0) { repeat = atoi(line.c_str() + 7); block.clear(); continue; }
    if (line == "end-repeat") { for (int i = 0; i < repeat; i++) lines.insert(lines.end(), block.begin(), block.end()); repeat = 0; continue; }
    if (repeat) block.push_back(line); else lines.push_back(line);
  }
  for (size_t index = options.resumeLine; index < lines.size(); index++) {
    lineNumber = index + 1;
    std::string line = lines[index];
    const auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;
    line = line.substr(start);
    const auto space = line.find(' ');
    const std::string command = line.substr(0, space);
    const std::string rest = space == std::string::npos ? "" : line.substr(space + 1);
    printf("[scenario] %.1f s: %s\n", millis() / 1000.0, line.c_str());
    if (command == "wait") sleepMs(atoi(rest.c_str()));
    else if (command == "press") {
      std::istringstream parts(rest);
      std::string name;
      while (parts >> name) {
        const auto found = namedKeys().find(name);
        Key key;
        if (found != namedKeys().end()) key = found->second;
        else if (name.size() != 1 || !findCharacter(name[0], key)) return fail("no key called " + name);
        pressKey(key);
      }
    } else if (command == "chord") {
      std::istringstream parts(rest);
      std::string first, second;
      parts >> first >> second;
      const Key a = namedKeys().at(first);
      Key b;
      if (namedKeys().count(second)) b = namedKeys().at(second); else if (!findCharacter(second[0], b)) return fail("no key " + second);
      keySet(a.x, a.y, true); sleepMs(40); keySet(b.x, b.y, true); sleepMs(kKeyHoldMs);
      keySet(b.x, b.y, false); sleepMs(40); keySet(a.x, a.y, false); sleepMs(kKeyHoldMs);
    } else if (command == "type") {
      for (const char c : unquote(rest)) {
        Key key;
        if (c == ' ') key = namedKeys().at("space");
        else if (!findCharacter(c, key)) return fail(std::string("cannot type ") + c);
        pressKey(key);
      }
    } else if (command == "button") { buttonPress(); sleepMs(100); }
    else if (command == "wifi") wifiAvailable = rest == "on";
    else if (command == "wait-text" || command == "expect-text") {
      std::istringstream parts(rest);
      const auto quote = rest.rfind('"');
      const std::string wanted = rest.substr(1, quote - 1);
      const int timeout = command == "expect-text" ? 0 : (quote + 1 < rest.size() ? atoi(rest.c_str() + quote + 1) : 10000);
      const uint32_t until = millis() + timeout;
      while (shown().find(wanted) == std::string::npos) {
        if (millis() >= until) return fail("screen never showed \"" + wanted + "\"");
        sleepMs(20);
      }
    } else if (command == "expect-no-text") {
      if (shown().find(unquote(rest)) != std::string::npos) return fail("screen shows \"" + unquote(rest) + "\"");
    } else if (command == "shot") {
      const std::string path = std::string(options.state) + "/" + rest;
      if (!savePng(path.c_str())) return fail("cannot save " + path);
    } else if (command == "reboot") {
      // A restart: a new process, with NVS and the card as they stand.
      char next[16];
      snprintf(next, sizeof(next), "%d", static_cast<int>(index + 1));
      std::vector<char *> args;
      for (char **arg = savedArgv; *arg; arg++) {
        if (!strcmp(*arg, "--resume")) { arg++; continue; }
        args.push_back(*arg);
      }
      const char *flag = "--resume";
      args.push_back(const_cast<char *>(flag));
      args.push_back(next);
      args.push_back(nullptr);
      fflush(stdout);
      execv("/proc/self/exe", args.data());
      return fail("reboot failed");
    } else if (command == "expect-crash") { expectedCrash = strdup(unquote(rest).c_str()); }
    else if (command == "expect-file") {
      const auto quote = rest.find('"');
      const std::string path = std::string(options.card) + "/" + rest.substr(0, quote - 1);
      const std::string wanted = rest.substr(quote + 1, rest.rfind('"') - quote - 1);
      std::ifstream target(path);
      std::stringstream contents;
      contents << target.rdbuf();
      if (!target || contents.str().find(wanted) == std::string::npos) return fail(path + " does not hold \"" + wanted + "\": [" + contents.str() + "]");
    } else if (command == "expect-file-gone") {
      if (std::ifstream(std::string(options.card) + "/" + rest)) return fail(rest + " still exists");
    } else if (command == "expect-audio") {
      if (audioSeconds() < atof(rest.c_str())) return fail("only " + std::to_string(audioSeconds()) + " s of audio played");
      if (audioLevel() < 1) return fail("the audio is silent");
    } else if (command == "expect-underruns") {
      if (audioUnderruns() > atoi(rest.c_str())) return fail(std::to_string(audioUnderruns()) + " underruns");
    } else if (command == "expect-heap-free") {
      if (static_cast<long>(heapFree()) < atol(rest.c_str())) return fail("free heap is " + std::to_string(heapFree()));
    } else if (command == "expect-heap-lowest") {
      if (static_cast<long>(heapLowest()) < atol(rest.c_str())) return fail("lowest free heap was " + std::to_string(heapLowest()));
    } else if (command == "expect-no-clipping") {
      if (clippedCount() > 0) return fail(std::to_string(clippedCount()) + " pieces of text ran off the screen");
    } else if (command == "show") { printf("[screen]\n%s\n", shown().c_str()); }
    else if (command == "report") { heapReport(rest.c_str()); stackReport(); printf("[audio] %.1f s played, %d underruns\n", audioSeconds(), audioUnderruns()); }
    else return fail("unknown command " + command);
  }
  if (expectedCrash) { printf("[scenario] FAILED: the expected crash (%s) never came\n", expectedCrash); return 1; }
  printf("[scenario] PASS\n");
  stackReport();
  heapReport("at end");
  printf("[audio] %.1f s played, %d underruns\n", audioSeconds(), audioUnderruns());
  return 0;
}

// A PNG with stored (uncompressed) deflate blocks: no library needed for a picture this small.
namespace {
uint32_t crc32Of(const uint8_t *data, size_t length, uint32_t crc = 0xFFFFFFFF) {
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
  }
  return crc;
}
void chunk(FILE *file, const char *type, const std::vector<uint8_t> &data) {
  const uint8_t size[4] = {uint8_t(data.size() >> 24), uint8_t(data.size() >> 16), uint8_t(data.size() >> 8), uint8_t(data.size())};
  fwrite(size, 1, 4, file);
  fwrite(type, 1, 4, file);
  if (!data.empty()) fwrite(data.data(), 1, data.size(), file);
  uint32_t crc = crc32Of(reinterpret_cast<const uint8_t *>(type), 4);
  crc = crc32Of(data.data(), data.size(), crc) ^ 0xFFFFFFFF;
  const uint8_t tail[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
  fwrite(tail, 1, 4, file);
}
}  // namespace

bool savePng(const char *path) {
  HostScope host;
  constexpr int width = 240, height = 135;
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(signature, 1, 8, file);
  std::vector<uint8_t> header = {0, 0, 0, width, 0, 0, 0, height, 8, 2, 0, 0, 0};
  chunk(file, "IHDR", header);
  std::vector<uint8_t> raw;
  raw.reserve((width * 3 + 1) * height);
  const uint16_t *pixels = screenPixels();
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    for (int x = 0; x < width; x++) {
      // LovyanGFX keeps 16 bit sprites byte-swapped.
      const uint16_t swapped = pixels[y * width + x];
      const uint16_t color = (swapped >> 8) | (swapped << 8);
      raw.push_back(((color >> 11) & 0x1F) * 255 / 31);
      raw.push_back(((color >> 5) & 0x3F) * 255 / 63);
      raw.push_back((color & 0x1F) * 255 / 31);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (const uint8_t value : raw) { a = (a + value) % 65521; b = (b + a) % 65521; }
  for (size_t at = 0; at < raw.size();) {
    const size_t take = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + take == raw.size());
    z.push_back(take & 255); z.push_back(take >> 8); z.push_back(~take & 255); z.push_back((~take >> 8) & 255);
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + take);
    at += take;
  }
  const uint32_t adler = (b << 16) | a;
  for (int shift = 24; shift >= 0; shift -= 8) z.push_back(adler >> shift);
  chunk(file, "IDAT", z);
  chunk(file, "IEND", {});
  fclose(file);
  return true;
}

}  // namespace emu
