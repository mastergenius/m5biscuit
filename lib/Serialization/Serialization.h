#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

static bool readString(FsFile& file, std::string& s, const uint32_t maxLen) {
  uint32_t len;
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) {
    s.clear();
    return false;
  }
  if (len > maxLen || len > static_cast<uint32_t>(file.available())) {
    s.clear();
    return false;
  }
  s.resize(len);
  if (len == 0) {
    return true;
  }
  if (file.read(&s[0], len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}

static void readString(FsFile& file, std::string& s) {
  constexpr uint32_t MAX_SERIALIZED_STRING_LEN = 8192;
  readString(file, s, MAX_SERIALIZED_STRING_LEN);
}
}  // namespace serialization
