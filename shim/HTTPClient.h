#pragma once
// HTTP over the shim's clients: the requests the firmware makes, nothing more.
#include <WiFi.h>
#define HTTP_CODE_OK 200
#define HTTP_CODE_PARTIAL_CONTENT 206
#define HTTPC_ERROR_CONNECTION_REFUSED -1
#define HTTPC_ERROR_CONNECTION_LOST -5
class HTTPClient {
 public:
  ~HTTPClient() { end(); }
  bool begin(const String &url);
  bool begin(WiFiClient &client, const String &url);
  void end();
  void addHeader(const String &name, const String &value);
  void useHTTP10(bool on = true) { http10_ = on; }
  void setReuse(bool) {}
  void setTimeout(uint32_t ms) { timeoutMs_ = ms; }
  void setConnectTimeout(int32_t) {}
  int GET();
  int POST(const String &body);
  int POST(const uint8_t *body, size_t size);
  int sendRequest(const char *method, uint8_t *body, size_t size);
  int sendRequest(const char *method, const String &body);
  int sendRequest(const char *method, Stream *body, size_t size);
  int getSize() const { return size_; }
  String getString();
  WiFiClient &getStream() { return *client_; }
  WiFiClient *getStreamPtr() { return client_; }

 private:
  int run(const char *method, const uint8_t *body, size_t size);
  void parseUrl(const String &url);
  WiFiClient *client_ = nullptr;
  WiFiClient own_;
  bool external_ = false;
  bool http10_ = false;
  bool chunked_ = false;
  bool secure_ = false;
  String host_;
  String path_;
  String headers_;
  uint16_t port_ = 80;
  int size_ = -1;
  uint32_t timeoutMs_ = 5000;
};
