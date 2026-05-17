#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>

#include <algorithm>
#include <cassert>

#include "HalBoard.h"
#include "HalSharedSpi.h"

#if BISCUIT_BOARD_M5PAPER
#include <SPI.h>
#include <SdFat.h>

#include "HalGPIO.h"
#else
#include <SDCardManager.h>
#define SDCard SDCardManager::getInstance()
#endif

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
#if BISCUIT_BOARD_M5PAPER
  HalSharedSpiLock spiLock;
#endif

 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  FsFile file;
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() {
  if (impl) {
    HalStorage::StorageLock lock;
    if (impl->file.isOpen()) {
      impl->file.close();
    }
  }
}

HalFile::HalFile(HalFile&& other) : impl(std::move(other.impl)) {}

HalFile& HalFile::operator=(HalFile&& other) {
  if (this != &other) {
    if (impl) {
      HalStorage::StorageLock lock;
      if (impl->file.isOpen()) {
        impl->file.close();
      }
    }
    impl = std::move(other.impl);
  }
  return *this;
}

#if BISCUIT_BOARD_M5PAPER

namespace {
SdFat sd;

bool isRootPath(const char* path) { return path && path[0] == '/' && path[1] == '\0'; }

bool makeParentDirectory(const char* path) {
  if (!path || path[0] == '\0') {
    return false;
  }
  String parent(path);
  const int slash = parent.lastIndexOf('/');
  if (slash <= 0) {
    return true;
  }
  parent = parent.substring(0, slash);
  return sd.exists(parent.c_str()) || sd.mkdir(parent.c_str(), true);
}

bool removeDirectoryRecursive(const char* path) {
  FsFile dir = sd.open(path, O_RDONLY);
  if (!dir || !dir.isDirectory()) {
    return false;
  }

  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    char name[128] = {};
    entry.getName(name, sizeof(name));
    String child(path);
    if (!child.endsWith("/")) {
      child += "/";
    }
    child += name;

    if (entry.isDirectory()) {
      entry.close();
      if (!removeDirectoryRecursive(child.c_str())) {
        dir.close();
        return false;
      }
    } else {
      entry.close();
      if (!sd.remove(child.c_str())) {
        dir.close();
        return false;
      }
    }
  }
  dir.close();
  return isRootPath(path) ? true : sd.rmdir(path);
}
}  // namespace

bool HalStorage::begin() {
  StorageLock lock;
  SPI.begin(M5PAPER_SPI_SCLK, M5PAPER_SPI_MISO, M5PAPER_SPI_MOSI, M5PAPER_SD_CS);
  initialized = sd.begin(SdSpiConfig(M5PAPER_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(25), &SPI));
  if (!initialized) {
    LOG_ERR("SD", "begin failed sdError=0x%02x data=0x%08lx", sd.sdErrorCode(),
            static_cast<unsigned long>(sd.sdErrorData()));
  }
  return initialized;
}

bool HalStorage::ready() const { return initialized; }

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  StorageLock lock;
  std::vector<String> files;
  FsFile dir = sd.open(path, O_RDONLY);
  if (!dir || !dir.isDirectory()) {
    return files;
  }

  FsFile entry;
  while ((maxFiles <= 0 || static_cast<int>(files.size()) < maxFiles) && entry.openNext(&dir, O_RDONLY)) {
    char name[128] = {};
    entry.getName(name, sizeof(name));
    files.emplace_back(name);
    entry.close();
  }
  dir.close();
  return files;
}

String HalStorage::readFile(const char* path) {
  StorageLock lock;
  FsFile file = sd.open(path, O_RDONLY);
  if (!file) {
    return String();
  }
  String content;
  content.reserve(file.fileSize());
  while (file.available()) {
    content += static_cast<char>(file.read());
  }
  file.close();
  return content;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  StorageLock lock;
  FsFile file = sd.open(path, O_RDONLY);
  if (!file) {
    return false;
  }

  uint8_t buffer[256];
  const size_t effectiveChunk = std::min(chunkSize, sizeof(buffer));
  while (file.available()) {
    const int bytesRead = file.read(buffer, effectiveChunk);
    if (bytesRead <= 0) {
      break;
    }
    if (out.write(buffer, bytesRead) != static_cast<size_t>(bytesRead)) {
      file.close();
      return false;
    }
  }
  file.close();
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0) {
    return 0;
  }
  StorageLock lock;
  FsFile file = sd.open(path, O_RDONLY);
  if (!file) {
    buffer[0] = '\0';
    return 0;
  }
  const size_t limit = maxBytes > 0 ? std::min(maxBytes, bufferSize - 1) : bufferSize - 1;
  const int bytesRead = file.read(buffer, limit);
  file.close();
  const size_t written = bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0;
  buffer[written] = '\0';
  return written;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  StorageLock lock;
  if (!makeParentDirectory(path)) {
    return false;
  }
  FsFile file = sd.open(path, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) {
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(content.c_str()), content.length()) == content.length();
  file.close();
  return ok;
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  StorageLock lock;
  return isRootPath(path) || sd.exists(path) || sd.mkdir(path, true);
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  if ((oflag & (O_WRITE | O_CREAT | O_TRUNC | O_APPEND)) != 0) {
    makeParentDirectory(path);
  }
  return HalFile(std::make_unique<HalFile::Impl>(sd.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  return sd.mkdir(path, pFlag);
}

bool HalStorage::exists(const char* path) {
  StorageLock lock;
  return sd.exists(path);
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  return sd.remove(path);
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  StorageLock lock;
  makeParentDirectory(newPath);
  return sd.rename(oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  return sd.rmdir(path);
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  FsFile fsFile = sd.open(path, O_RDONLY);
  const bool ok = static_cast<bool>(fsFile);
  if (!ok) {
    LOG_ERR(moduleName ? moduleName : "SD", "open read failed: %s sdError=0x%02x data=0x%08lx", path,
            sd.sdErrorCode(), static_cast<unsigned long>(sd.sdErrorData()));
  }
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  if (!makeParentDirectory(path)) {
    LOG_ERR(moduleName ? moduleName : "SD", "parent directory create failed for: %s", path);
  }
  FsFile fsFile = sd.open(path, O_WRITE | O_CREAT | O_TRUNC);
  const bool ok = static_cast<bool>(fsFile);
  if (!ok) {
    LOG_ERR(moduleName ? moduleName : "SD", "open write failed: %s sdError=0x%02x data=0x%08lx", path,
            sd.sdErrorCode(), static_cast<unsigned long>(sd.sdErrorData()));
  }
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  return removeDirectoryRecursive(path);
}

#else

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  return HalFile(std::make_unique<HalFile::Impl>(SDCard.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

#endif

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::isOpen() const {
  if (!impl) {
    return false;
  }
  HalStorage::StorageLock lock;
  return impl->file.isOpen();
}
HalFile::operator bool() const { return isOpen(); }
