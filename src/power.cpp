// Battery voltage predicted from a device's fitted power model (tools/fit-power.py), driven by
// what the firmware is doing. With no model loaded this reports "uncalibrated" and no numbers.
#include <ArduinoJson.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "emu.h"

namespace emu {
namespace {
// MEASURED sdkconfig: CONFIG_ESP32S3_BROWNOUT_DET_LVL 7, which ESP-IDF's Kconfig gives as 2.44 V at the
// chip's 3.3 V rail. The rail follows the battery only below the regulator's dropout, which nothing
// has measured yet; a brownout seen by the lab gives the battery voltage directly and replaces this.
constexpr int kBrownoutRailMv = 2440;
constexpr int kAssumedDropoutMv = 300;

bool loaded = false, synthetic = false;
std::string deviceName;
double resistanceOhm = 0, tauSeconds = 1, capacityMah = 1750, soc = 1, sagMv = 0;
std::vector<std::pair<double, double>> ocvCurve;
std::map<std::string, double> currents, peakSag;
int brownoutBatteryMv = kBrownoutRailMv + kAssumedDropoutMv;
std::atomic<int> brightness{255}, volume{192};
std::atomic<double> mp3Activity{0};
std::atomic<int> tlsHandshakes{0}, streaming{0};
double lowestMv = 1e9, usedMah = 0, seconds = 0, lastCurrent = 0;
std::atomic<bool> belowBrownout{false};

double current(const std::string &action, double fallback = 0) { const auto at = currents.find(action); return at == currents.end() ? fallback : at->second; }
double ocv(double level) {
  if (ocvCurve.empty()) return 3700;
  double best = ocvCurve.front().second, bestGap = 2;
  for (const auto &point : ocvCurve) if (std::fabs(point.first - level) < bestGap) { bestGap = std::fabs(point.first - level); best = point.second; }
  return best;
}
const char *brightnessAction(int value) { return value < 32 ? "idle_b0" : value < 96 ? "idle_b64" : value < 160 ? "idle_b128" : value < 224 ? "idle_b192" : "idle_b255"; }
const char *speakerAction(int value) { return value < 96 ? "spk_speech_64" : value < 160 ? "spk_speech_128" : value < 224 ? "spk_speech_192" : "spk_speech_255"; }

void modelThread() {
  markHostThread();
  double peakExtra = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(100 / options.timeScale));
    const double idle = current(brightnessAction(brightness), current("idle_b64"));
    double total = idle;
    peakExtra = 0;
    const bool radio = wifiAvailable;
    if (radio) total += std::max(0.0, current("wifi_idle") - current("idle_b64"));
    if (streaming > 0) total += std::max(0.0, current("download") - current("wifi_idle"));
    if (tlsHandshakes > 0) total += std::max(0.0, current("tls_handshake") - current("wifi_idle"));
    const double playing = audioLevel() > 1 ? 1 : 0;
    if (playing > 0) {
      total += std::max(0.0, current(speakerAction(volume)) - current("idle_b64"));
      const auto peak = peakSag.find(speakerAction(volume));
      if (peak != peakSag.end()) peakExtra = peak->second;
    }
    lastCurrent = total;
    usedMah += total * 0.1 / 3600;
    seconds += 0.1;
    sagMv += (total * resistanceOhm - sagMv) * (1 - std::exp(-0.1 / tauSeconds));
    soc = std::max(0.0, 1 - usedMah / capacityMah);
    const double volts = ocv(soc) - std::max(sagMv, peakExtra);
    if (volts < lowestMv) lowestMv = volts;
    if (volts < brownoutBatteryMv) belowBrownout = true;
  }
}
}  // namespace

void powerLoad(const char *path, double startLevel) {
  HostScope host;
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  JsonDocument document;
  if (!file || deserializeJson(document, text.str())) { printf("[power] cannot read %s\n", path); return; }
  deviceName = document["device"] | "?";
  synthetic = document["synthetic"] | false;
  resistanceOhm = document["resistance_ohm"] | 0.0;
  tauSeconds = document["recovery_tau_s"] | 1.0;
  capacityMah = document["capacity_mah_assumed"] | 1750.0;
  for (JsonArrayConst point : document["ocv_by_soc"].as<JsonArrayConst>()) ocvCurve.emplace_back(point[0].as<double>(), point[1].as<double>());
  for (JsonPairConst entry : document["current_ma_by_action"].as<JsonObjectConst>()) currents[entry.key().c_str()] = entry.value().as<double>();
  for (JsonPairConst entry : document["peak_sag_mv_by_action"].as<JsonObjectConst>()) peakSag[entry.key().c_str()] = entry.value().as<double>();
  for (JsonVariantConst value : document["battery_mv_at_brownout"].as<JsonArrayConst>()) brownoutBatteryMv = value.as<int>();
  loaded = true;
  soc = startLevel;
  usedMah = capacityMah * (1 - startLevel);
  std::thread(modelThread).detach();
}

void powerSetBrightness(int value) { brightness = value; }
void powerSetVolume(int value) { volume = value; }
void powerStreaming(int delta) { streaming += delta; }
void powerHandshake(int delta) { tlsHandshakes += delta; }

void powerReport() {
  if (!loaded) { printf("[power] uncalibrated: no measured model for this device, so no voltage or battery prediction\n"); return; }
  const double hours = lastCurrent > 0 ? capacityMah * soc / lastCurrent : 0;
  printf("[power] %s %s: predicted %.0f mV at %.0f%% charge, lowest %.0f mV, drawing %.0f mA inferred, %.1f h left at this load; browns out near %d mV%s\n",
         synthetic ? "SYNTHETIC, not a measurement," : "device", deviceName.c_str(), ocv(soc) - sagMv, soc * 100, lowestMv, lastCurrent, hours, brownoutBatteryMv,
         belowBrownout ? "  BELOW IT" : "");
}
bool powerOk() { return !loaded || !belowBrownout; }
bool powerLoaded() { return loaded; }
}  // namespace emu
