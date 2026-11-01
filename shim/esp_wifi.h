#pragma once
#include <stdint.h>
typedef enum { WIFI_SECOND_CHAN_NONE = 0 } wifi_second_chan_t;
typedef enum { WIFI_IF_STA = 0 } wifi_interface_t;
typedef enum { WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC } wifi_promiscuous_pkt_type_t;
struct wifi_pkt_rx_ctrl_t { int rssi; int channel; int sig_len; };
struct wifi_promiscuous_pkt_t { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[1]; };
typedef void (*wifi_promiscuous_cb_t)(void *, wifi_promiscuous_pkt_type_t);
inline int esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return 0; }
inline int esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second) { *primary = 1; *second = WIFI_SECOND_CHAN_NONE; return 0; }
inline int esp_wifi_set_promiscuous(bool) { return 0; }
inline int esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t) { return 0; }
