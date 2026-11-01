#pragma once
// The microSD card: a host folder, charged what an open file costs on the device.
#include <Arduino.h>
#include <SPI.h>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
class File : public Stream {
 public:
  File() = default;
  File(const File &other);
  File &operator=(const File &other);
  ~File() override;
  operator bool() const { return state_ != nullptr; }
  int available() override;
  int read() override;
  int peek() override;
  size_t read(uint8_t *buffer, size_t size);
  size_t readBytes(char *buffer, size_t length) override { return read(reinterpret_cast<uint8_t *>(buffer), length); }
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  void flush() override;
  bool seek(uint32_t position);
  size_t size();
  size_t position();
  void close();
  bool isDirectory();
  File openNextFile();
  const char *name();
  const char *path();
  struct State;
  State *state_ = nullptr;
};
class SDClass {
 public:
  bool begin(int cs, SPIClass &spi, uint32_t frequency);
  File open(const char *path, const char *mode = FILE_READ);
  File open(const String &path, const char *mode = FILE_READ) { return open(path.c_str(), mode); }
  bool exists(const char *path);
  bool exists(const String &path) { return exists(path.c_str()); }
  bool remove(const char *path);
  bool remove(const String &path) { return remove(path.c_str()); }
  bool rename(const char *from, const char *to);
  bool rename(const String &from, const String &to) { return rename(from.c_str(), to.c_str()); }
};
extern SDClass SD;
