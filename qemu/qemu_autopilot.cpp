// Compiled into the QEMU build only. QEMU has no Cardputer keyboard, so this task presses play:
// it runs the real player, decoder, card and heap on the real binary and logs what they do.
// With /squeeze on the card it first uses up RAM, to test what running out does on the real code.
#ifdef QEMU_AUTOPILOT
#include <Arduino.h>
#include <SD.h>

#include "media_player.h"
#include "os.h"

namespace {
constexpr uint32_t kSqueezeTo = 30 * 1024;

const char *stateName(media::State state) {
  switch (state) {
    case media::State::Idle: return "IDLE";
    case media::State::Connecting: return "BUFFERING";
    case media::State::Playing: return "PLAYING";
    case media::State::Paused: return "PAUSED";
    case media::State::Ended: return "ENDED";
    default: return "FAILED";
  }
}

void autopilotTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(8000));
  Serial.printf("AUTOPILOT start free=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (SD.exists("/squeeze")) {
    while (ESP.getFreeHeap() > kSqueezeTo + 1024) {
      if (!heap_caps_malloc(1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) break;
    }
    Serial.printf("AUTOPILOT squeezed free=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  if (!media::begin()) Serial.println("AUTOPILOT no player task");
  media::playFile("/books/Test/001 Test.mp3", 0);
  media::State last = media::State::Idle;
  bool played = false;
  for (int second = 0; second < 40; second++) {
    const media::State now = media::state();
    if (now != last) {
      last = now;
      Serial.printf("AUTOPILOT state=%s at=%us free=%u failure=%s\n", stateName(now), media::positionSeconds(), ESP.getFreeHeap(), media::failure());
    }
    if (now == media::State::Playing) played = true;
    if (now == media::State::Ended || now == media::State::Failed) break;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  Serial.printf("AUTOPILOT done played=%d state=%s position=%u lowest=%u failure=%s\n", played, stateName(media::state()), media::positionSeconds(), media::lowestHeap(), media::failure());
  media::stop();
  vTaskDelay(pdMS_TO_TICKS(500));
  media::end();
  Serial.printf("AUTOPILOT end free=%u\n", ESP.getFreeHeap());
  vTaskDelete(nullptr);
}

struct Start {
  Start() { xTaskCreatePinnedToCore(autopilotTask, "autopilot", 4096, nullptr, 1, nullptr, 1); }
};
Start start;
}  // namespace
#endif
