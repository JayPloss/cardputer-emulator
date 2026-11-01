#pragma once
#include <stdint.h>
#include "esp_wifi.h"
#define ESP_OK 0
#define ESP_NOW_MAX_DATA_LEN 250
struct esp_now_peer_info_t { uint8_t peer_addr[6]; uint8_t channel; wifi_interface_t ifidx; bool encrypt; };
typedef void (*esp_now_recv_cb_t)(const uint8_t *, const uint8_t *, int);
inline int esp_now_init() { return ESP_OK; }
inline int esp_now_deinit() { return ESP_OK; }
inline int esp_now_register_recv_cb(esp_now_recv_cb_t) { return ESP_OK; }
inline bool esp_now_is_peer_exist(const uint8_t *) { return true; }
inline int esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_OK; }
inline int esp_now_send(const uint8_t *, const uint8_t *, size_t) { return ESP_OK; }
