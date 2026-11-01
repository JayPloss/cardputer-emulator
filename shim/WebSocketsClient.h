#pragma once
// The walkie socket: holds a TLS session's RAM while open. No audio crosses it.
#include <Arduino.h>
typedef enum { WStype_ERROR, WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT, WStype_BIN } WStype_t;
class WebSocketsClient {
 public:
  typedef void (*Event)(WStype_t, uint8_t *, size_t);
  void setExtraHeaders(const char *) {}
  void onEvent(Event event) { event_ = event; }
  void setReconnectInterval(unsigned long) {}
  void beginSSL(const char *host, uint16_t port, const char *path, const char *fingerprint = "");
  void disconnect();
  void loop();
  bool sendBIN(const uint8_t *, size_t) { return true; }
  bool sendTXT(const char *) { return true; }
  bool sendTXT(const String &) { return true; }

 private:
  Event event_ = nullptr;
  void *in_ = nullptr;
  void *out_ = nullptr;
  void *context_ = nullptr;
  bool wanted_ = false;
  uint32_t retryAt_ = 0;
};
