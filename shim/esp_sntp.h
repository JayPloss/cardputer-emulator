#pragma once
// SNTP's sync callback. configTzTime calls it when a joined network lets a time server answer,
// as the device's SNTP does once a server replies; a network with no internet never calls it.
#include <sys/time.h>
typedef void (*sntp_sync_time_cb_t)(struct timeval *tv);
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback);
// lwIP's SNTP client as ESP-IDF 4.4 runs it (src/arduino_impl.cpp): asks when started, again on
// sntp_restart(), every CONFIG_LWIP_SNTP_UPDATE_DELAY (3 h) after an answer, and after a failed ask
// 15 s later, doubling to 150 s (lwIP's SNTP_RETRY_TIMEOUT defaults). An answer goes through
// sntp_sync_time, which the firmware may replace (ESP-IDF makes it weak): the default sets the clock
// and calls the notification callback.
typedef enum { SNTP_SYNC_STATUS_RESET, SNTP_SYNC_STATUS_COMPLETED, SNTP_SYNC_STATUS_IN_PROGRESS } sntp_sync_status_t;
extern "C" void sntp_sync_time(struct timeval *tv);
void sntp_set_sync_status(sntp_sync_status_t status);
sntp_sync_status_t sntp_get_sync_status();
bool sntp_restart();
