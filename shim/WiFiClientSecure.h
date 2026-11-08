#pragma once
// TLS is modelled, not done: connecting holds what mbedTLS would hold, and the
// replies come from an in-process stand-in for supachat.net. Production is never
// contacted, and no real messages or device tokens are involved.
#include <WiFi.h>
class WiFiClientSecure : public WiFiClient {
 public:
  WiFiClientSecure() = default;
  ~WiFiClientSecure() override;
  void setInsecure() { chain_ = false; }
  void setCACert(const char *) { chain_ = true; }
  void setHandshakeTimeout(unsigned long) {}
  int connect(const char *host, uint16_t port) override;
  int connect(const char *host, uint16_t port, int32_t timeoutMs);
  int connect(IPAddress ip, uint16_t port) override { (void)ip; return connect("", port); }
  // Certificates are modelled, not checked: on a "certrenewed" network the leaf pin no longer
  // matches but the chain does (what v0.48 met on Oct 9, 2026); on "badcert" (a filter that
  // re-signs TLS) neither does. Elsewhere both pass.
  bool verify(const char *, const char *);
  int lastError(char *buffer, size_t size);
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
  bool chain_ = false;
  const char *error_ = "";
  struct Exchange;
  Exchange *exchange_ = nullptr;
};
