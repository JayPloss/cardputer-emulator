#pragma once
// SNTP's sync callback. configTzTime calls it when a joined network lets a time server answer,
// as the device's SNTP does once a server replies; a network with no internet never calls it.
#include <sys/time.h>
typedef void (*sntp_sync_time_cb_t)(struct timeval *tv);
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback);
