#pragma once
// Wi-Fi as the firmware sees it: one network, joined at once, using the host's sockets.
// Nothing about the radio is modelled.
#include <Arduino.h>
#include <Client.h>
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_OFF = 0, WIFI_STA = 1 } wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;
class WiFiClass {
 public:
  void mode(wifi_mode_t) {}
  void setSleep(bool) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void disconnect(bool = false, bool = false);
  wl_status_t begin(const char *ssid, const char *password);
  wl_status_t status();
  int scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300, uint8_t = 0);
  void scanDelete() {}
  String SSID();
  String SSID(int);
  int32_t RSSI(int = 0);
  wifi_auth_mode_t encryptionType(int) { return WIFI_AUTH_WPA2_PSK; }
  uint8_t *BSSID(int = 0);
  int32_t channel(int = 0) { return 6; }
};
extern WiFiClass WiFi;

class WiFiClient : public Client {
 public:
  WiFiClient() = default;
  ~WiFiClient() override;
  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char *host, uint16_t port) override;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  int available() override;
  int read() override;
  int read(uint8_t *buffer, size_t size) override;
  int peek() override;
  void flush() override {}
  void stop() override;
  uint8_t connected() override;
  operator bool() override { return connected(); }
  void setTimeout(uint32_t seconds) { Stream::setTimeout(seconds * 1000); timeoutSeconds_ = seconds; }

 protected:
  bool fill();
  int fd_ = -1;
  bool eof_ = false;
  void *socketBlock_ = nullptr;
  void *windowBlock_ = nullptr;
  uint8_t buffer_[1436];
  int bufferStart_ = 0;
  int bufferEnd_ = 0;
  uint32_t timeoutSeconds_ = 30;
};
