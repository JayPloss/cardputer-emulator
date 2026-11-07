// Wi-Fi, sockets and HTTP on the host, the TLS memory model, and the SupaChat stand-in.
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFiClientSecure.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "device_model.h"
#include "emu.h"

WiFiClass WiFi;

namespace {
struct SimNet { std::string ssid, password, behaviour; };
std::mutex netLock;
std::vector<SimNet> nets = {{"EMU-LAN", "emu", "home"}};
std::string joinedSsid, joinedBehaviour;
std::atomic<bool> joined{false};
std::atomic<int> scansRunning{0};

std::string behaviourNow() {
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(netLock);
  return joinedBehaviour;
}
bool plainBlackholed() { emu::HostScope host; const std::string b = behaviourNow(); return b == "nonav" || b == "captive" || b == "nointernet"; }
}  // namespace

namespace emu {
void netAdd(const char *ssid, const char *password, const char *behaviour) {
  HostScope host;
  std::lock_guard<std::mutex> guard(netLock);
  nets.push_back({ssid, password, behaviour});
}
bool ntpReachable() {
  if (!joined) return false;
  const std::string b = behaviourNow();
  return b.empty() || b == "home" || b == "nonav";
}
void netClear() {
  HostScope host;
  std::lock_guard<std::mutex> guard(netLock);
  nets.clear();
}
}  // namespace emu

void WiFiClass::disconnect(bool, bool) { joined = false; }
wl_status_t WiFiClass::begin(const char *ssid, const char *password) {
  emu::sleepMs(1000);
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(netLock);
  for (const auto &net : nets) {
    if (net.ssid == ssid && net.password == password && net.behaviour != "joinfail") {
      joinedSsid = net.ssid; joinedBehaviour = net.behaviour;
      joined = emu::wifiAvailable;
      return joined ? WL_CONNECTED : WL_DISCONNECTED;
    }
  }
  joined = false;
  return WL_DISCONNECTED;
}
wl_status_t WiFiClass::status() {
  if (!emu::wifiAvailable) joined = false;
  return joined ? WL_CONNECTED : WL_DISCONNECTED;
}
int WiFiClass::scanNetworks(bool, bool, bool, uint32_t, uint8_t) {
  // As on the device, a second scan while one runs fails at once.
  if (scansRunning.fetch_add(1) > 0) { scansRunning--; return -1; }
  emu::sleepMs(2000);
  scansRunning--;
  if (!emu::wifiAvailable) return 0;
  emu::HostScope host;
  std::lock_guard<std::mutex> guard(netLock);
  return nets.size();
}
String WiFiClass::SSID() {
  if (!joined) return String();
  char copy[40];
  { emu::HostScope host; std::lock_guard<std::mutex> guard(netLock); snprintf(copy, sizeof(copy), "%s", joinedSsid.c_str()); }
  return String(copy);
}
String WiFiClass::SSID(int index) {
  char copy[40] = "";
  { emu::HostScope host; std::lock_guard<std::mutex> guard(netLock); if (index >= 0 && index < static_cast<int>(nets.size())) snprintf(copy, sizeof(copy), "%s", nets[index].ssid.c_str()); }
  return String(copy);
}
int32_t WiFiClass::RSSI(int index) { return -50 - index; }
uint8_t *WiFiClass::BSSID(int) { static uint8_t bssid[6] = {2, 0, 0, 0, 0, 1}; return bssid; }

// ---- sockets ----

WiFiClient::~WiFiClient() { stop(); }
int WiFiClient::connect(IPAddress ip, uint16_t port) { return connect(ip.toString().c_str(), port); }
int WiFiClient::connect(const char *host, uint16_t port) { return connectWithin(host, port, 5000); }
int WiFiClient::connectWithin(const char *host, uint16_t port, uint32_t timeoutMs) {
  stop();
  if (WiFi.status() != WL_CONNECTED) return 0;
  // The address goes nowhere on this network: the attempt uses up its whole timeout.
  if (plainBlackholed()) { emu::sleepMs(timeoutMs); return 0; }
  socketBlock_ = emu::deviceAlloc(model::kSocketBytes, "socket");
  if (!socketBlock_) return 0;
  emu::HostScope hostScope;
  emu::Blocked blocked;
  addrinfo hints{}, *found = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &found) != 0) { stop(); return 0; }
  fd_ = socket(found->ai_family, found->ai_socktype, 0);
  if (fd_ >= 0) {
    timeval timeout{5, 0};
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    const int result = ::connect(fd_, found->ai_addr, found->ai_addrlen);
    if (result != 0) { close(fd_); fd_ = -1; }
  }
  freeaddrinfo(found);
  if (fd_ < 0) { stop(); return 0; }
  eof_ = false;
  bufferStart_ = bufferEnd_ = 0;
  return 1;
}
size_t WiFiClient::write(const uint8_t *data, size_t size) {
  if (fd_ < 0) return 0;
  emu::Blocked blocked;
  const ssize_t sent = ::send(fd_, data, size, MSG_NOSIGNAL);
  return sent > 0 ? sent : 0;
}
bool WiFiClient::fill() {
  if (fd_ < 0 || eof_) return false;
  if (bufferStart_ < bufferEnd_) return true;
  // lwIP keeps what the TCP window holds in pbufs, so a live stream owns that much RAM.
  if (!windowBlock_) { windowBlock_ = emu::deviceAlloc(model::kTcpWindowBytes, "tcp window"); if (windowBlock_) emu::powerStreaming(1); }
  pollfd ready{fd_, POLLIN, 0};
  if (poll(&ready, 1, 0) <= 0) return false;
  const ssize_t got = ::recv(fd_, buffer_, sizeof(buffer_), 0);
  if (got == 0) { eof_ = true; return false; }
  if (got < 0) return false;
  bufferStart_ = 0;
  bufferEnd_ = got;
  return true;
}
int WiFiClient::available() { fill(); return bufferEnd_ - bufferStart_; }
int WiFiClient::read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
int WiFiClient::read(uint8_t *out, size_t size) {
  if (!fill()) return -1;
  const int count = min<int>(size, bufferEnd_ - bufferStart_);
  memcpy(out, buffer_ + bufferStart_, count);
  bufferStart_ += count;
  return count;
}
int WiFiClient::peek() { return fill() ? buffer_[bufferStart_] : -1; }
uint8_t WiFiClient::connected() {
  fill();
  return bufferStart_ < bufferEnd_ || (fd_ >= 0 && !eof_);
}
void WiFiClient::stop() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  emu::deviceFree(socketBlock_); socketBlock_ = nullptr;
  if (windowBlock_) emu::powerStreaming(-1);
  emu::deviceFree(windowBlock_); windowBlock_ = nullptr;
  bufferStart_ = bufferEnd_ = 0;
}

// ---- TLS: what it holds, and a stand-in for supachat.net ----

namespace {
// Stands in for supachat.net, with the response shapes of server.mjs and the sizes of the
// real family room: 804 messages, mostly a few characters long, a few at the 140 limit,
// client ids of about 19 characters. The messages themselves are made up.
constexpr int kRoomMessages = 804;
std::string queryValue(const std::string &request, const std::string &name) {
  const auto end = request.find(" HTTP/");
  auto at = request.find(name + "=");
  if (at == std::string::npos || at > end) return "";
  at += name.size() + 1;
  return request.substr(at, request.find_first_of("& ", at) - at);
}
std::string messageJson(int id, const std::string &room) {
  // Lengths repeat to give an average of about 6, as measured, with the occasional long one.
  static const int lengths[] = {2, 3, 4, 5, 3, 7, 2, 4, 6, 3, 2, 5, 9, 3, 4, 2, 6, 3, 28, 140};
  const int length = lengths[id % 20];
  const std::string body(length, "abcdefgh"[id % 8]);
  const std::string author = id % 3 == 0 ? "test2" : "test1";
  return "{\"id\":" + std::to_string(id) + ",\"client_id\":\"emu-000000-" + std::to_string(100000 + id) + "\",\"conversation_id\":\"" + room +
         "\",\"author_id\":\"" + author + "\",\"author_name\":\"" + (id % 3 == 0 ? "Other" : "Test") + "\",\"type\":\"text\",\"body\":\"" + body +
         "\",\"created_at\":" + std::to_string(1790000000000LL + id * 60000LL) + ",\"reply_to\":null,\"receipt_state\":\"read\"}";
}
std::string syncReply(const std::string &request) {
  const std::string room = queryValue(request, "room");
  const int limit = std::max(1, atoi(queryValue(request, "limit").c_str()));
  const std::string before = queryValue(request, "before");
  std::string messages, tail;
  if (!before.empty()) {
    // History, a page at a time, oldest of the page first.
    const int ceiling = before == "0" ? kRoomMessages + 1 : atoi(before.c_str());
    const int oldest = std::max(1, ceiling - limit);
    for (int id = oldest; id < ceiling; id++) { if (!messages.empty()) messages += ","; messages += messageJson(id, room); }
    tail = ",\"history\":{\"has_more\":" + std::string(oldest > 1 ? "true" : "false") + ",\"next_before\":" + std::to_string(oldest) + "}";
  } else {
    const int after = atoi(queryValue(request, "after").c_str());
    for (int id = after + 1; id <= kRoomMessages && id <= after + limit; id++) { if (!messages.empty()) messages += ","; messages += messageJson(id, room); }
  }
  return "{\"server_time\":1790000000000,\"room_id\":\"" + room + "\",\"messages\":[" + messages + "],\"receipts\":[]" + tail + "}";
}
std::string roomsReply() {
  return "{\"rooms\":[{\"id\":\"family\",\"name\":\"Family\",\"kind\":\"room\",\"group_id\":\"g1\",\"group_name\":\"Family\",\"default_language\":\"en\",\"latest_message_id\":804,\"unread_count\":0,\"color_index\":1},"
         "{\"id\":\"k-buds\",\"name\":\"K-BUDS\",\"kind\":\"room\",\"group_id\":\"g2\",\"group_name\":\"Friends\",\"default_language\":\"en\",\"latest_message_id\":218,\"unread_count\":0,\"color_index\":2}]}";
}
}  // namespace

// mbedTLS asks for its handshake memory in many small pieces over the handshake's length,
// so a device short of RAM runs right down to nothing before the handshake fails. That
// window is when anyone else's small allocation fails.
static bool holdHandshake(const char *tag) {
  constexpr int kPieces = 48;
  void *pieces[kPieces] = {};
  bool ok = true;
  emu::powerHandshake(1);
  for (int i = 0; i < kPieces && ok; i++) {
    pieces[i] = emu::deviceAlloc(model::kTlsHandshakeExtra / kPieces, tag);
    ok = pieces[i] != nullptr;
    if (ok) emu::chargeMs(model::kTlsHandshakeMs / kPieces);
  }
  emu::powerHandshake(-1);
  for (void *piece : pieces) emu::deviceFree(piece);
  return ok;
}

struct WiFiClientSecure::Exchange {
  std::string request;
  std::string reply;
  size_t at = 0;
  bool answered = false;
};

WiFiClientSecure::~WiFiClientSecure() { stop(); }
int WiFiClientSecure::connect(const char *host, uint16_t port) { return connect(host, port, 30000); }
int WiFiClientSecure::connect(const char *, uint16_t, int32_t timeoutMs) {
  stop();
  if (WiFi.status() != WL_CONNECTED) return 0;
  {
    const std::string behaviour = behaviourNow();
    if (behaviour == "nointernet") { emu::sleepMs(timeoutMs); return 0; }
    if (behaviour == "captive") { emu::sleepMs(400); return 0; }  // The portal answers TLS with plain HTTP.
  }
  socketBlock_ = emu::deviceAlloc(model::kSocketBytes, "socket");
  context_ = emu::deviceAlloc(model::kTlsContext, "tls context");
  in_ = emu::deviceAlloc(model::kTlsRecordBuffer, "tls in");
  out_ = emu::deviceAlloc(model::kTlsRecordBuffer, "tls out");
  windowBlock_ = emu::deviceAlloc(model::kTlsTrafficBytes, "tls traffic");
  const bool ok = socketBlock_ && context_ && in_ && out_ && windowBlock_ && holdHandshake("tls handshake");
  if (!ok) { stop(); return 0; }
  emu::HostScope host;
  exchange_ = new Exchange;
  open_ = true;
  return 1;
}
size_t WiFiClientSecure::write(const uint8_t *data, size_t size) {
  if (!open_) return 0;
  emu::HostScope host;
  exchange_->request.append(reinterpret_cast<const char *>(data), size);
  return size;
}
void WiFiClientSecure::prepareReply() {
  if (!exchange_ || exchange_->answered || exchange_->request.find("\r\n\r\n") == std::string::npos) return;
  emu::HostScope host;
  exchange_->answered = true;
  const std::string &request = exchange_->request;
  const std::string body = request.find("/api/rooms") != std::string::npos ? roomsReply()
                           : request.find("/api/device/sync") != std::string::npos ? syncReply(request) : "{}";
  exchange_->reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  emu::chargeMs(40);
}
int WiFiClientSecure::available() { prepareReply(); return exchange_ ? exchange_->reply.size() - exchange_->at : 0; }
int WiFiClientSecure::read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
int WiFiClientSecure::read(uint8_t *data, size_t size) {
  prepareReply();
  if (!exchange_ || exchange_->at >= exchange_->reply.size()) return -1;
  const size_t count = min(size, exchange_->reply.size() - exchange_->at);
  memcpy(data, exchange_->reply.data() + exchange_->at, count);
  exchange_->at += count;
  return count;
}
int WiFiClientSecure::peek() { prepareReply(); return exchange_ && exchange_->at < exchange_->reply.size() ? static_cast<uint8_t>(exchange_->reply[exchange_->at]) : -1; }
uint8_t WiFiClientSecure::connected() { prepareReply(); return open_ && exchange_ && (!exchange_->answered || exchange_->at < exchange_->reply.size()); }
void WiFiClientSecure::stop() {
  emu::deviceFree(context_); context_ = nullptr;
  emu::deviceFree(in_); in_ = nullptr;
  emu::deviceFree(out_); out_ = nullptr;
  if (exchange_) { emu::HostScope host; delete exchange_; exchange_ = nullptr; }
  open_ = false;
  WiFiClient::stop();
}

// ---- the walkie socket ----

void WebSocketsClient::beginSSL(const char *, uint16_t, const char *, const char *) { wanted_ = true; retryAt_ = 0; }
void WebSocketsClient::disconnect() {
  wanted_ = false;
  const bool wasOpen = in_ != nullptr;
  emu::deviceFree(in_); in_ = nullptr;
  emu::deviceFree(out_); out_ = nullptr;
  emu::deviceFree(context_); context_ = nullptr;
  if (wasOpen && event_) event_(WStype_DISCONNECTED, nullptr, 0);
}
void WebSocketsClient::loop() {
  if (!wanted_ || in_ || emu::millis() < retryAt_) return;
  context_ = emu::deviceAlloc(model::kTlsContext, "ws tls context");
  in_ = emu::deviceAlloc(model::kTlsRecordBuffer, "ws tls in");
  out_ = emu::deviceAlloc(model::kTlsRecordBuffer, "ws tls out");
  void *traffic = emu::deviceAlloc(model::kTlsTrafficBytes, "ws tls traffic");
  const bool ok = context_ && in_ && out_ && traffic && holdHandshake("ws tls handshake");
  emu::deviceFree(traffic);
  if (!ok) {
    emu::deviceFree(in_); in_ = nullptr;
    emu::deviceFree(out_); out_ = nullptr;
    emu::deviceFree(context_); context_ = nullptr;
    retryAt_ = emu::millis() + 2000;
    if (event_) event_(WStype_ERROR, nullptr, 0);
    return;
  }
  if (event_) event_(WStype_CONNECTED, nullptr, 0);
}

// ---- HTTP ----

void HTTPClient::parseUrl(const String &url) {
  secure_ = url.startsWith("https://");
  const int start = url.indexOf("://") + 3;
  int slash = url.indexOf('/', start);
  if (slash < 0) slash = url.length();
  String authority = url.substring(start, slash);
  path_ = slash < static_cast<int>(url.length()) ? url.substring(slash) : String("/");
  const int colon = authority.indexOf(':');
  port_ = secure_ ? 443 : 80;
  if (colon >= 0) { port_ = authority.substring(colon + 1).toInt(); authority = authority.substring(0, colon); }
  host_ = authority;
}
bool HTTPClient::begin(const String &url) { parseUrl(url); external_ = false; client_ = &own_; headers_ = ""; return true; }
bool HTTPClient::begin(WiFiClient &client, const String &url) { parseUrl(url); external_ = true; client_ = &client; headers_ = ""; return true; }
void HTTPClient::end() {
  if (client_ && !external_) own_.stop();
  client_ = nullptr;
}
void HTTPClient::addHeader(const String &name, const String &value) { headers_ += name + ": " + value + "\r\n"; }
int HTTPClient::GET() { return run("GET", nullptr, 0); }
int HTTPClient::POST(const String &body) { return run("POST", reinterpret_cast<const uint8_t *>(body.c_str()), body.length()); }
int HTTPClient::POST(const uint8_t *body, size_t size) { return run("POST", body, size); }
int HTTPClient::sendRequest(const char *method, uint8_t *body, size_t size) { return run(method, body, size); }
int HTTPClient::sendRequest(const char *method, Stream *body, size_t size) {
  // Voice clip uploads: read and sent, with the stand-in answering afterwards.
  uint8_t chunk[256];
  size_t left = size;
  int status = run(method, nullptr, 0);
  while (left > 0) { const size_t got = body->readBytes(reinterpret_cast<char *>(chunk), min(left, sizeof(chunk))); if (!got) break; left -= got; }
  return status;
}
int HTTPClient::sendRequest(const char *method, const String &body) { return run(method, reinterpret_cast<const uint8_t *>(body.c_str()), body.length()); }
int HTTPClient::run(const char *method, const uint8_t *body, size_t size) {
  if (!client_) return HTTPC_ERROR_CONNECTION_REFUSED;
  if (!client_->connected() && !client_->connectWithin(host_.c_str(), port_, connectTimeoutMs_)) return HTTPC_ERROR_CONNECTION_REFUSED;
  String request = String(method) + " " + path_ + (http10_ ? " HTTP/1.0\r\n" : " HTTP/1.1\r\n") + "Host: " + host_ + "\r\nUser-Agent: ESP32HTTPClient\r\nConnection: close\r\n" + headers_;
  if (size) request += "Content-Length: " + String(static_cast<unsigned>(size)) + "\r\n";
  request += "\r\n";
  client_->write(reinterpret_cast<const uint8_t *>(request.c_str()), request.length());
  if (size) client_->write(body, size);
  chunked_ = false;
  size_ = -1;
  int status = 0;
  const uint32_t deadline = emu::millis() + timeoutMs_;
  String line;
  bool first = true;
  while (emu::millis() < deadline) {
    const int c = client_->read();
    if (c < 0) {
      if (!client_->connected()) return HTTPC_ERROR_CONNECTION_LOST;
      emu::sleepMs(1);
      continue;
    }
    if (c != '\n') { if (c != '\r') line += static_cast<char>(c); continue; }
    if (first) { status = line.substring(line.indexOf(' ') + 1).toInt(); first = false; }
    else if (line.isEmpty()) return status;
    else {
      String lower = line; lower.toLowerCase();
      if (lower.startsWith("content-length:")) size_ = lower.substring(15).toInt();
      if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) chunked_ = true;
    }
    line = "";
  }
  return HTTPC_ERROR_CONNECTION_LOST;
}
String HTTPClient::getString() {
  String out;
  if (!client_) return out;
  if (size_ > 0) out.reserve(size_);
  const uint32_t deadline = emu::millis() + timeoutMs_;
  uint8_t chunk[513];  // String::concat copies the terminator too.
  while (emu::millis() < deadline && (size_ < 0 || static_cast<int>(out.length()) < size_)) {
    const int got = client_->read(chunk, sizeof(chunk) - 1);
    if (got > 0) chunk[got] = 0;
    if (got > 0) { out.concat(reinterpret_cast<const char *>(chunk), got); continue; }
    if (!client_->connected()) break;
    emu::sleepMs(1);
  }
  return out;
}
