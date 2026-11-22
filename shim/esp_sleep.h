#pragma once
// ESP-IDF light sleep as the firmware uses it. esp_light_sleep_start blocks the calling task, as the chip
// stops, until its timer runs out or a wake pin goes low: here a held key (the ADV keyboard chip pulls its
// interrupt line, GPIO11, low) or a rear button press (GPIO0). Time asleep is counted for `expect-asleep`.
// Not covered: the real chip's current in light sleep, the pins' electrical behaviour, and what the USB
// serial connection does across a sleep.
#include <stdint.h>

typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_TIMER = 4,
  ESP_SLEEP_WAKEUP_GPIO = 7,
} esp_sleep_wakeup_cause_t;
typedef int esp_err_t;

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_sleep_enable_gpio_wakeup();
esp_err_t esp_light_sleep_start();
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause();
