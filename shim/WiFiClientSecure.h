#pragma once
// TLS is modelled, not done: connecting holds what mbedTLS would hold, and the
// replies come from an in-process stand-in for supachat.net. Production is never
// contacted, and no real messages or device tokens are involved.
#include <WiFi.h>
class WiFiClientSecure : public WiFiClient {
 public:
  WiFiClientSecure() = default;
  ~WiFiClientSecure() override;
  void setInsecure() {}
  void setCACert(const char *) {}
  void setHandshakeTimeout(unsigned long) {}
  int connect(const char *host, uint16_t port) override;
  int connect(const char *host, uint16_t port, int32_t timeoutMs);
  int connect(IPAddress ip, uint16_t port) override { (void)ip; return connect("", port); }
  bool verify(const char *, const char *) { return true; }
  int lastError(char *buffer, size_t size) { if (size) buffer[0] = 0; return 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  int available() override;
  int read() override;
  int read(uint8_t *buffer, size_t size) override;
  int peek() override;
  void stop() override;
  uint8_t connected() override;

 private:
  void prepareReply();
  void *in_ = nullptr;
  void *out_ = nullptr;
  void *context_ = nullptr;
  bool open_ = false;
  struct Exchange;
  Exchange *exchange_ = nullptr;
};
