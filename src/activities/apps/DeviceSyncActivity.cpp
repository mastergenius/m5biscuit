#include "DeviceSyncActivity.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalBoard.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/md.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <new>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "study/StudyPackReader.h"
#include "study/StudyReviewLog.h"
#include "util/RadioManager.h"

#if __has_include("DeviceSyncDebugTemporaryPairingFallback.local.h")
#include "DeviceSyncDebugTemporaryPairingFallback.local.h"
#define BISCUIT_HAS_DEVICE_SYNC_DEBUG_TEMPORARY_PAIRING_FALLBACK 1
#else
#define BISCUIT_HAS_DEVICE_SYNC_DEBUG_TEMPORARY_PAIRING_FALLBACK 0
#endif

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "unknown"
#endif

namespace {
constexpr const char* MODULE = "DSYNC";
constexpr const char* STUDY_PACK_ROOT = StudyPackReader::PACK_ROOT;
constexpr size_t MAX_SYNC_PACK_FILE_BYTES = 64UL * 1024UL * 1024UL;
constexpr size_t DOWNLOAD_BUFFER_BYTES = 2048;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 15000;
constexpr uint32_t SYNC_TASK_STACK_BYTES = 32UL * 1024UL;
constexpr uint32_t MAX_SYNC_STUDY_LOG_BYTES = 16 * 1024;
constexpr uint8_t MAX_SYNC_STUDY_LOG_SEGMENTS = 1;
constexpr uint8_t MAX_SYNC_ROUNDS = 32;
constexpr uint32_t MAX_SYNC_TOTAL_MS = 120000;

bool isValidPackId(const char* value) {
  if (!value || value[0] == '\0') {
    return false;
  }
  const size_t len = strlen(value);
  if (len < 2 || len > 96 || !std::isalnum(static_cast<unsigned char>(value[0]))) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    if (!(std::isalnum(ch) || value[i] == '-' || value[i] == '_' || value[i] == '.')) {
      return false;
    }
  }
  return true;
}

std::string trimTrailingSlash(std::string value) {
  while (value.size() > 1 && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned char>(c));
          out += esc;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

std::string quoted(const std::string& value) { return "\"" + jsonEscape(value) + "\""; }

bool hexToBytes(const std::string& hex, uint8_t* out, const size_t outLen) {
  if (hex.size() != outLen * 2) {
    return false;
  }
  for (size_t i = 0; i < outLen; i++) {
    const char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char* end = nullptr;
    const unsigned long value = strtoul(pair, &end, 16);
    if (!end || *end != '\0' || value > 255) {
      return false;
    }
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

std::string bytesToHex(const uint8_t* data, const size_t len) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = digits[data[i] >> 4];
    out[i * 2 + 1] = digits[data[i] & 0x0f];
  }
  return out;
}

std::string hmacSha256Hex(const std::string& secretHex, const std::string& message) {
  uint8_t key[32];
  if (!hexToBytes(secretHex, key, sizeof(key))) {
    return "";
  }

  uint8_t digest[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    return "";
  }
  if (mbedtls_md_hmac(info, key, sizeof(key), reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                      digest) != 0) {
    return "";
  }
  return bytesToHex(digest, sizeof(digest));
}

std::string sha256Hex(const String& value) {
  uint8_t digest[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    return "";
  }
  if (mbedtls_md(info, reinterpret_cast<const unsigned char*>(value.c_str()), value.length(), digest) != 0) {
    return "";
  }
  return bytesToHex(digest, sizeof(digest));
}

std::string sha256Hex(const std::string& value) {
  uint8_t digest[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    return "";
  }
  if (mbedtls_md(info, reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest) != 0) {
    return "";
  }
  return bytesToHex(digest, sizeof(digest));
}

bool fileSize(const std::string& path, size_t& sizeOut) {
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) {
    return false;
  }
  sizeOut = file.size();
  file.close();
  return true;
}

bool feedFilePrefixToHash(const std::string& path, const size_t bytesToRead, mbedtls_md_context_t& ctx,
                          size_t& bytesRead) {
  bytesRead = 0;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) {
    return false;
  }
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[DOWNLOAD_BUFFER_BYTES]);
  if (!buffer) {
    file.close();
    return false;
  }
  while (bytesRead < bytesToRead) {
    const size_t wanted = std::min(DOWNLOAD_BUFFER_BYTES, bytesToRead - bytesRead);
    const int got = file.read(buffer.get(), wanted);
    if (got <= 0) {
      file.close();
      return false;
    }
    if (mbedtls_md_update(&ctx, buffer.get(), static_cast<size_t>(got)) != 0) {
      file.close();
      return false;
    }
    bytesRead += static_cast<size_t>(got);
  }
  file.close();
  return true;
}

std::string sha256FileHex(const std::string& path, const size_t expectedSize = 0) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    return "";
  }
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  uint8_t digest[32];
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) {
    mbedtls_md_free(&ctx);
    return "";
  }
  size_t bytesRead = 0;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[DOWNLOAD_BUFFER_BYTES]);
  if (!buffer) {
    file.close();
    mbedtls_md_free(&ctx);
    return "";
  }
  if (mbedtls_md_setup(&ctx, info, 0) != 0 || mbedtls_md_starts(&ctx) != 0) {
    file.close();
    mbedtls_md_free(&ctx);
    return "";
  }
  while (true) {
    const int got = file.read(buffer.get(), DOWNLOAD_BUFFER_BYTES);
    if (got < 0) {
      file.close();
      mbedtls_md_free(&ctx);
      return "";
    }
    if (got == 0) {
      break;
    }
    if (mbedtls_md_update(&ctx, buffer.get(), static_cast<size_t>(got)) != 0) {
      file.close();
      mbedtls_md_free(&ctx);
      return "";
    }
    bytesRead += static_cast<size_t>(got);
  }
  file.close();
  if ((expectedSize != 0 && bytesRead != expectedSize) || mbedtls_md_finish(&ctx, digest) != 0) {
    mbedtls_md_free(&ctx);
    return "";
  }
  mbedtls_md_free(&ctx);
  return bytesToHex(digest, sizeof(digest));
}

std::string deviceIdFromMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  if (mac.isEmpty() || mac == "000000000000") {
    mac = "unknown";
  }
#if BISCUIT_BOARD_M5PAPER
  return std::string("m5paper-") + mac.c_str();
#else
  return std::string("xteink-") + mac.c_str();
#endif
}

std::string localIpString() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  const IPAddress ip = WiFi.localIP();
  char buf[20];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return buf;
}

std::string buildSyncPayload(const std::vector<StudyPackInfo>& installedPacks,
                             const std::vector<StudyReviewLog::SyncSegment>& studySegments) {
  size_t reserveBytes = 320;
  reserveBytes += installedPacks.size() * 128;
  for (const auto& segment : studySegments) {
    reserveBytes += segment.name.size() + segment.content.size() + 192;
  }
  std::string payload;
  payload.reserve(reserveBytes);
  payload += "{\"installed_study_packs\":[";
  for (size_t i = 0; i < installedPacks.size(); i++) {
    if (i > 0) {
      payload += ",";
    }
    payload += "{\"id\":";
    payload += quoted(installedPacks[i].id);
    payload += ",\"revision\":";
    payload += quoted(installedPacks[i].revision);
    payload += "}";
  }
  payload += "],\"status\":{";
  payload += "\"battery\":";
  payload += std::to_string(static_cast<unsigned>(powerManager.getBatteryPercentage()));
  payload += ",\"firmware\":";
  payload += quoted(CROSSPOINT_VERSION);
  payload += ",\"free_heap\":";
  payload += std::to_string(static_cast<unsigned long>(esp_get_free_heap_size()));
  payload += ",\"ip\":";
  payload += quoted(localIpString());
  payload += ",\"rssi\":";
  payload += std::to_string(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  payload += ",\"uptime_ms\":";
  payload += std::to_string(static_cast<unsigned long>(millis()));
  payload += "}";
  if (!studySegments.empty()) {
    payload += ",\"study_logs\":[";
    for (size_t i = 0; i < studySegments.size(); i++) {
      if (i > 0) {
        payload += ",";
      }
      // Field order must match host canonical_json(sort_keys=True); payload JSON is signed as built.
      payload += "{\"content\":";
      payload += quoted(studySegments[i].content);
      payload += ",\"last_seq\":";
      payload += std::to_string(static_cast<unsigned long>(studySegments[i].lastSeq));
      payload += ",\"segment\":";
      payload += quoted(studySegments[i].name);
      payload += ",\"sha256\":";
      payload += quoted(sha256Hex(studySegments[i].content));
      payload += "}";
    }
    payload += "]";
  }
  payload += "}";
  return payload;
}

std::string canonicalMessage(const std::string& deviceId, const std::string& nonce, const std::string& payloadJson) {
  std::string message;
  message.reserve(deviceId.size() + nonce.size() + payloadJson.size() + 64);
  message += "{\"device_id\":";
  message += quoted(deviceId);
  message += ",\"nonce\":";
  message += quoted(nonce);
  message += ",\"payload\":";
  message += payloadJson;
  message += "}";
  return message;
}

std::string bootstrapCanonicalMessage(const std::string& deviceId, const std::string& nonce) {
  std::string message;
  message.reserve(deviceId.size() + nonce.size() + 80);
  message += "{\"device_id\":";
  message += quoted(deviceId);
  message += ",\"nonce\":";
  message += quoted(nonce);
  message += ",\"purpose\":\"device-bootstrap-v0\"}";
  return message;
}

std::string buildHostConfigJson(const std::string& hostUrl, const std::string& deviceId, const std::string& secretHex) {
  std::string config;
  config.reserve(hostUrl.size() + deviceId.size() + secretHex.size() + 128);
  config += "{\n";
  config += "  \"schema\": \"biscuit.device-sync-host.v0\",\n";
  config += "  \"host_url\": ";
  config += quoted(hostUrl);
  config += ",\n";
  config += "  \"device_id\": ";
  config += quoted(deviceId);
  config += ",\n";
  config += "  \"secret\": ";
  config += quoted(secretHex);
  config += "\n";
  config += "}\n";
  return config;
}

bool readJsonStringField(const JsonDocument& doc, const char* key, std::string& out) {
  const char* value = doc[key] | nullptr;
  if (!value || value[0] == '\0') {
    return false;
  }
  out = value;
  return true;
}

bool removeDirIfExists(const std::string& path) {
  return !Storage.exists(path.c_str()) || Storage.removeDir(path.c_str());
}

bool validatePackManifest(const char* manifestText, const char* expectedPackId, const char* expectedRevision) {
  if (!manifestText || !expectedPackId || !expectedRevision) {
    return false;
  }
  JsonDocument manifest;
  if (deserializeJson(manifest, manifestText)) {
    return false;
  }
  const char* schema = manifest["schema"] | "";
  const char* packId = manifest["pack_id"] | "";
  const char* revision = manifest["revision"] | "";
  return strcmp(schema, "biscuit.study-pack.v0") == 0 && strcmp(packId, expectedPackId) == 0 &&
         strcmp(revision, expectedRevision) == 0;
}

bool writePackFile(const std::string& dir, const char* fileName, const char* content) {
  if (!fileName || !content) {
    return false;
  }
  const size_t len = strlen(content);
  if (len == 0 || len > MAX_SYNC_PACK_FILE_BYTES) {
    LOG_ERR(MODULE, "received pack file has invalid size: %s (%u bytes)", fileName, static_cast<unsigned>(len));
    return false;
  }
  const std::string path = dir + "/" + fileName;
  return Storage.writeFileAtomic(path.c_str(), String(content));
}

bool writePackFileString(const std::string& dir, const char* fileName, const String& content) {
  if (!fileName) {
    return false;
  }
  const size_t len = content.length();
  if (len == 0 || len > MAX_SYNC_PACK_FILE_BYTES) {
    LOG_ERR(MODULE, "received pack file has invalid size: %s (%u bytes)", fileName, static_cast<unsigned>(len));
    return false;
  }
  const std::string path = dir + "/" + fileName;
  return Storage.writeFileAtomic(path.c_str(), content);
}

bool installStudyPack(JsonObjectConst pack) {
  const char* packId = pack["pack_id"] | "";
  const char* revision = pack["revision"] | "";
  JsonObjectConst files = pack["files"].as<JsonObjectConst>();
  const char* manifest = files["manifest.json"] | nullptr;
  const char* concepts = files["concepts.jsonl"] | nullptr;
  const char* episodes = files["episodes.jsonl"] | nullptr;
  const char* rubrics = files["rubrics.jsonl"] | nullptr;

  if (!isValidPackId(packId) || !revision[0] || files.isNull() || !manifest || !concepts || !episodes || !rubrics) {
    LOG_ERR(MODULE, "received invalid StudyPack envelope");
    return false;
  }
  if (!validatePackManifest(manifest, packId, revision)) {
    LOG_ERR(MODULE, "received StudyPack manifest mismatch: %s", packId);
    return false;
  }
  if (!Storage.ensureDirectoryExists(STUDY_PACK_ROOT)) {
    LOG_ERR(MODULE, "could not create StudyPack root");
    return false;
  }

  const std::string safeId(packId);
  const std::string target = std::string(STUDY_PACK_ROOT) + "/" + safeId;
  const std::string incoming = std::string(STUDY_PACK_ROOT) + "/.incoming_" + safeId;
  const std::string backup = std::string(STUDY_PACK_ROOT) + "/.backup_" + safeId;

  if (!removeDirIfExists(incoming) || !removeDirIfExists(backup)) {
    LOG_ERR(MODULE, "could not clean StudyPack staging dirs: %s", packId);
    return false;
  }
  if (!Storage.ensureDirectoryExists(incoming.c_str())) {
    LOG_ERR(MODULE, "could not create StudyPack staging dir: %s", packId);
    return false;
  }

  if (!writePackFile(incoming, "manifest.json", manifest) || !writePackFile(incoming, "concepts.jsonl", concepts) ||
      !writePackFile(incoming, "episodes.jsonl", episodes) || !writePackFile(incoming, "rubrics.jsonl", rubrics)) {
    removeDirIfExists(incoming);
    return false;
  }

  const bool hadTarget = Storage.exists(target.c_str());
  if (hadTarget && !Storage.rename(target.c_str(), backup.c_str())) {
    LOG_ERR(MODULE, "could not move old StudyPack aside: %s", packId);
    removeDirIfExists(incoming);
    return false;
  }
  if (!Storage.rename(incoming.c_str(), target.c_str())) {
    LOG_ERR(MODULE, "could not install StudyPack: %s", packId);
    if (hadTarget) {
      Storage.rename(backup.c_str(), target.c_str());
    }
    removeDirIfExists(incoming);
    return false;
  }
  removeDirIfExists(backup);
  LOG_INF(MODULE, "Installed StudyPack %s rev=%s", packId, revision);
  return true;
}

bool isAllowedPackFileName(const char* fileName) {
  return fileName && (strcmp(fileName, "manifest.json") == 0 || strcmp(fileName, "concepts.jsonl") == 0 ||
                      strcmp(fileName, "episodes.jsonl") == 0 || strcmp(fileName, "rubrics.jsonl") == 0);
}

int getText(const String& url, String& response, const uint16_t timeoutMs) {
  response = "";
  HTTPClient http;
  if (!http.begin(url)) {
    LOG_ERR(MODULE, "HTTP begin failed: %s", url.c_str());
    return -1000;
  }
  http.setReuse(false);
  http.useHTTP10(true);
  http.setConnectTimeout(5000);
  http.setTimeout(timeoutMs);
  http.addHeader("Connection", "close");
  const int code = http.GET();
  response = http.getString();
  http.end();
  return code;
}

String artifactUrlFor(const std::string& hostUrl, const char* pathOrUrl) {
  if (!pathOrUrl) {
    return "";
  }
  if (strncmp(pathOrUrl, "http://", 7) == 0 || strncmp(pathOrUrl, "https://", 8) == 0) {
    return String(pathOrUrl);
  }
  if (pathOrUrl[0] == '/') {
    return String(hostUrl.c_str()) + pathOrUrl;
  }
  return String(hostUrl.c_str()) + "/" + pathOrUrl;
}

std::string partMetaPathFor(const std::string& partPath) { return partPath + ".json"; }

void removePartialDownload(const std::string& partPath) {
  if (Storage.exists(partPath.c_str())) {
    Storage.remove(partPath.c_str());
  }
  const std::string metaPath = partMetaPathFor(partPath);
  if (Storage.exists(metaPath.c_str())) {
    Storage.remove(metaPath.c_str());
  }
}

bool partialMetadataMatches(const std::string& partPath, const char* expectedSha, const size_t expectedSize) {
  const std::string metaPath = partMetaPathFor(partPath);
  char buf[256];
  const size_t read = Storage.readFileToBuffer(metaPath.c_str(), buf, sizeof(buf), sizeof(buf) - 1);
  if (read == 0) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, buf)) {
    return false;
  }
  const char* sha = doc["sha256"] | "";
  const size_t size = doc["size"] | 0;
  return strcmp(sha, expectedSha) == 0 && size == expectedSize;
}

bool writePartialMetadata(const std::string& partPath, const char* expectedSha, const size_t expectedSize) {
  std::string body;
  body.reserve(128);
  body += "{\"sha256\":";
  body += quoted(expectedSha);
  body += ",\"size\":";
  body += std::to_string(static_cast<unsigned long>(expectedSize));
  body += "}\n";
  return Storage.writeFileAtomic(partMetaPathFor(partPath).c_str(), String(body.c_str()));
}

bool existingFileMatches(const std::string& path, const char* expectedSha, const size_t expectedSize) {
  size_t actualSize = 0;
  if (!fileSize(path, actualSize) || actualSize != expectedSize) {
    return false;
  }
  const std::string actualSha = sha256FileHex(path, expectedSize);
  return !actualSha.empty() && actualSha == expectedSha;
}

bool streamHttpToPartial(const String& fullUrl, const std::string& partPath, const size_t resumeOffset,
                         const size_t expectedSize, mbedtls_md_context_t& hashCtx, size_t& downloadedBytes) {
  downloadedBytes = resumeOffset;
  HTTPClient http;
  if (!http.begin(fullUrl)) {
    LOG_ERR(MODULE, "HTTP begin failed: %s", fullUrl.c_str());
    return false;
  }
  http.setReuse(false);
  http.useHTTP10(true);
  http.setConnectTimeout(5000);
  http.setTimeout(DOWNLOAD_IDLE_TIMEOUT_MS);
  http.addHeader("Connection", "close");
  if (resumeOffset > 0) {
    std::string range = "bytes=" + std::to_string(static_cast<unsigned long>(resumeOffset)) + "-";
    http.addHeader("Range", range.c_str());
  }

  const int code = http.GET();
  if ((resumeOffset == 0 && code != 200) || (resumeOffset > 0 && code != 206)) {
    LOG_ERR(MODULE, "artifact stream failed code=%d resume=%lu", code, static_cast<unsigned long>(resumeOffset));
    http.end();
    return false;
  }

  HalFile out = Storage.open(partPath.c_str(), O_WRITE | O_CREAT);
  if (!out) {
    LOG_ERR(MODULE, "could not open partial artifact for write: %s", partPath.c_str());
    http.end();
    return false;
  }
  if (!out.seek(resumeOffset)) {
    LOG_ERR(MODULE, "could not seek partial artifact: %s", partPath.c_str());
    out.close();
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[DOWNLOAD_BUFFER_BYTES]);
  if (!buffer) {
    LOG_ERR(MODULE, "could not allocate download buffer");
    out.close();
    http.end();
    return false;
  }
  uint32_t lastDataMs = millis();
  while (downloadedBytes < expectedSize) {
    const int available = stream ? stream->available() : 0;
    if (available > 0) {
      const size_t wanted = std::min(std::min(DOWNLOAD_BUFFER_BYTES, static_cast<size_t>(available)),
                                     expectedSize - downloadedBytes);
      const int got = stream->readBytes(buffer.get(), wanted);
      if (got <= 0) {
        break;
      }
      const size_t wrote = out.write(buffer.get(), static_cast<size_t>(got));
      if (wrote != static_cast<size_t>(got)) {
        LOG_ERR(MODULE, "partial artifact write failed: %s", partPath.c_str());
        out.close();
        http.end();
        return false;
      }
      if (mbedtls_md_update(&hashCtx, buffer.get(), static_cast<size_t>(got)) != 0) {
        out.close();
        http.end();
        return false;
      }
      downloadedBytes += static_cast<size_t>(got);
      lastDataMs = millis();
      delay(0);
      continue;
    }
    if (!http.connected()) {
      break;
    }
    if (millis() - lastDataMs > DOWNLOAD_IDLE_TIMEOUT_MS) {
      LOG_ERR(MODULE, "artifact stream idle timeout");
      out.close();
      http.end();
      return false;
    }
    delay(1);
  }
  out.flush();
  out.close();
  http.end();
  return downloadedBytes == expectedSize;
}

bool downloadArtifactFile(const std::string& hostUrl, JsonObjectConst file, const std::string& incoming) {
  const char* fileName = file["name"] | "";
  const char* url = file["url"] | "";
  const char* expectedSha = file["sha256"] | "";
  const size_t expectedSize = file["size"] | 0;

  if (!isAllowedPackFileName(fileName) || !url[0] || strlen(expectedSha) != 64 || expectedSize == 0 ||
      expectedSize > MAX_SYNC_PACK_FILE_BYTES) {
    LOG_ERR(MODULE, "invalid StudyPack artifact file descriptor: %s", fileName);
    return false;
  }

  const std::string finalPath = incoming + "/" + fileName;
  const std::string partPath = finalPath + ".part";
  if (Storage.exists(finalPath.c_str())) {
    if (existingFileMatches(finalPath, expectedSha, expectedSize)) {
      return true;
    }
    Storage.remove(finalPath.c_str());
  }

  if (Storage.exists(partPath.c_str()) && !partialMetadataMatches(partPath, expectedSha, expectedSize)) {
    removePartialDownload(partPath);
  }
  if (!Storage.exists(partPath.c_str()) && !writePartialMetadata(partPath, expectedSha, expectedSize)) {
    LOG_ERR(MODULE, "could not write partial metadata: %s", fileName);
    return false;
  }

  size_t resumeOffset = 0;
  if (Storage.exists(partPath.c_str())) {
    fileSize(partPath, resumeOffset);
    if (resumeOffset > expectedSize) {
      removePartialDownload(partPath);
      resumeOffset = 0;
      if (!writePartialMetadata(partPath, expectedSha, expectedSize)) {
        return false;
      }
    }
  }

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    return false;
  }
  mbedtls_md_context_t hashCtx;
  mbedtls_md_init(&hashCtx);
  if (mbedtls_md_setup(&hashCtx, info, 0) != 0 || mbedtls_md_starts(&hashCtx) != 0) {
    mbedtls_md_free(&hashCtx);
    return false;
  }
  if (resumeOffset > 0) {
    size_t hashedBytes = 0;
    if (!feedFilePrefixToHash(partPath, resumeOffset, hashCtx, hashedBytes) || hashedBytes != resumeOffset) {
      LOG_ERR(MODULE, "could not hash partial artifact prefix: %s", fileName);
      mbedtls_md_free(&hashCtx);
      removePartialDownload(partPath);
      return false;
    }
  }

  const String fullUrl = artifactUrlFor(hostUrl, url);
  size_t downloadedBytes = 0;
  bool ok = streamHttpToPartial(fullUrl, partPath, resumeOffset, expectedSize, hashCtx, downloadedBytes);
  if (!ok && resumeOffset > 0) {
    LOG_ERR(MODULE, "artifact resume failed; retrying from zero: %s", fileName);
    mbedtls_md_free(&hashCtx);
    removePartialDownload(partPath);
    if (!writePartialMetadata(partPath, expectedSha, expectedSize)) {
      return false;
    }
    mbedtls_md_init(&hashCtx);
    if (mbedtls_md_setup(&hashCtx, info, 0) != 0 || mbedtls_md_starts(&hashCtx) != 0) {
      mbedtls_md_free(&hashCtx);
      return false;
    }
    ok = streamHttpToPartial(fullUrl, partPath, 0, expectedSize, hashCtx, downloadedBytes);
  }
  if (!ok) {
    mbedtls_md_free(&hashCtx);
    return false;
  }

  uint8_t digest[32];
  if (downloadedBytes != expectedSize || mbedtls_md_finish(&hashCtx, digest) != 0) {
    mbedtls_md_free(&hashCtx);
    return false;
  }
  mbedtls_md_free(&hashCtx);
  const std::string actualSha = bytesToHex(digest, sizeof(digest));
  if (actualSha.empty() || actualSha != expectedSha) {
    LOG_ERR(MODULE, "artifact sha256 mismatch file=%s", fileName);
    removePartialDownload(partPath);
    return false;
  }
  if (Storage.exists(finalPath.c_str()) && !Storage.remove(finalPath.c_str())) {
    return false;
  }
  if (!Storage.rename(partPath.c_str(), finalPath.c_str())) {
    LOG_ERR(MODULE, "could not finalize artifact file: %s", fileName);
    return false;
  }
  const std::string metaPath = partMetaPathFor(partPath);
  if (Storage.exists(metaPath.c_str())) {
    Storage.remove(metaPath.c_str());
  }
  LOG_INF(MODULE, "Downloaded artifact %s bytes=%lu", fileName, static_cast<unsigned long>(expectedSize));
  return true;
}

bool installStudyPackArtifact(JsonObjectConst pack, const std::string& hostUrl) {
  const char* packId = pack["pack_id"] | "";
  const char* revision = pack["revision"] | "";
  JsonArrayConst files = pack["files"].as<JsonArrayConst>();

  if (!isValidPackId(packId) || !revision[0] || files.isNull()) {
    LOG_ERR(MODULE, "received invalid StudyPack artifact envelope");
    return false;
  }
  if (!Storage.ensureDirectoryExists(STUDY_PACK_ROOT)) {
    LOG_ERR(MODULE, "could not create StudyPack root");
    return false;
  }

  const std::string safeId(packId);
  const std::string target = std::string(STUDY_PACK_ROOT) + "/" + safeId;
  const std::string incoming = std::string(STUDY_PACK_ROOT) + "/.incoming_" + safeId;
  const std::string backup = std::string(STUDY_PACK_ROOT) + "/.backup_" + safeId;

  if (!removeDirIfExists(backup)) {
    LOG_ERR(MODULE, "could not clean StudyPack staging dirs: %s", packId);
    return false;
  }
  if (!Storage.ensureDirectoryExists(incoming.c_str())) {
    LOG_ERR(MODULE, "could not create StudyPack staging dir: %s", packId);
    return false;
  }

  bool seenManifest = false;
  bool seenConcepts = false;
  bool seenEpisodes = false;
  bool seenRubrics = false;
  for (JsonObjectConst file : files) {
    const char* fileName = file["name"] | "";
    if (strcmp(fileName, "manifest.json") == 0) {
      seenManifest = true;
    } else if (strcmp(fileName, "concepts.jsonl") == 0) {
      seenConcepts = true;
    } else if (strcmp(fileName, "episodes.jsonl") == 0) {
      seenEpisodes = true;
    } else if (strcmp(fileName, "rubrics.jsonl") == 0) {
      seenRubrics = true;
    }
    if (!downloadArtifactFile(hostUrl, file, incoming)) {
      return false;
    }
  }

  if (!seenManifest || !seenConcepts || !seenEpisodes || !seenRubrics) {
    LOG_ERR(MODULE, "StudyPack artifact missing required files: %s", packId);
    removeDirIfExists(incoming);
    return false;
  }

  const std::string manifestPath = incoming + "/manifest.json";
  const String manifest = Storage.readFile(manifestPath.c_str());
  if (!validatePackManifest(manifest.c_str(), packId, revision)) {
    LOG_ERR(MODULE, "received StudyPack manifest mismatch: %s", packId);
    removeDirIfExists(incoming);
    return false;
  }

  const bool hadTarget = Storage.exists(target.c_str());
  if (hadTarget && !Storage.rename(target.c_str(), backup.c_str())) {
    LOG_ERR(MODULE, "could not move old StudyPack aside: %s", packId);
    removeDirIfExists(incoming);
    return false;
  }
  if (!Storage.rename(incoming.c_str(), target.c_str())) {
    LOG_ERR(MODULE, "could not install StudyPack: %s", packId);
    if (hadTarget) {
      Storage.rename(backup.c_str(), target.c_str());
    }
    removeDirIfExists(incoming);
    return false;
  }
  removeDirIfExists(backup);
  LOG_INF(MODULE, "Installed StudyPack artifact %s rev=%s", packId, revision);
  return true;
}

bool removeStudyPackById(const char* packId) {
  if (!isValidPackId(packId)) {
    return false;
  }
  const std::string path = std::string(STUDY_PACK_ROOT) + "/" + packId;
  if (!Storage.exists(path.c_str())) {
    return true;
  }
  return Storage.removeDir(path.c_str());
}

size_t studyLogPayloadBytes(const std::vector<StudyReviewLog::SyncSegment>& studySegments) {
  size_t bytes = 0;
  for (const auto& segment : studySegments) {
    bytes += segment.content.size();
  }
  return bytes;
}

int postJson(const String& url, const std::string& body, String& response, const uint16_t timeoutMs) {
  response = "";
  HTTPClient http;
  if (!http.begin(url)) {
    LOG_ERR(MODULE, "HTTP begin failed: %s", url.c_str());
    return -1000;
  }
  http.setReuse(false);
  http.useHTTP10(true);
  http.setConnectTimeout(5000);
  http.setTimeout(timeoutMs);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  const int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  response = http.getString();
  http.end();
  return code;
}

int postJsonWithRetry(const String& url, const std::string& body, String& response, const uint16_t timeoutMs,
                      const uint8_t attempts = 2) {
  int code = -1;
  for (uint8_t attempt = 1; attempt <= attempts; attempt++) {
    code = postJson(url, body, response, timeoutMs);
    if (code >= 0 || attempt == attempts) {
      return code;
    }
    LOG_ERR(MODULE, "HTTP POST failed code=%d attempt=%u/%u; retrying", code, attempt, attempts);
    delay(250);
  }
  return code;
}
}  // namespace

void DeviceSyncActivity::onEnter() {
  Activity::onEnter();
  resetResultStats();
  state = LOADING_CONFIG;
  statusTitle = "Device Sync";
  statusDetail = "Loading config...";
  requestUpdate();
  loadConfigAndStart();
}

void DeviceSyncActivity::onExit() {
  Activity::onExit();
  statusDetail.clear();
}

void DeviceSyncActivity::resetResultStats() {
  syncedHostId.clear();
  ackedStatus = false;
  ackedNotes = 0;
  sentStudyLogs = 0;
  ackedStudyLogs = 0;
  receivedStudyPacks = 0;
  removedStudyPacks = 0;
  remainingStudyPacks = 0;
  pairedDuringSync = false;
  syncHasMore = false;
}

void DeviceSyncActivity::loadConfigAndStart() {
  needsBootstrapPairing = false;
  bootstrapSecretHex.clear();

  auto startFallbackBootstrap = [this]() {
    if (!loadDebugTemporaryPairingFallback()) {
      return false;
    }
    requestUpdate();
    startWifiThenSync();
    return true;
  };

  char buf[1024];
  const size_t read = Storage.readFileToBuffer(CONFIG_PATH, buf, sizeof(buf), sizeof(buf) - 1);
  if (read == 0) {
    if (startFallbackBootstrap()) {
      return;
    }
    fail("Missing config", std::string(CONFIG_PATH));
    state = NEED_CONFIG;
    requestUpdate();
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    LOG_ERR(MODULE, "host config parse failed; trying firmware bootstrap fallback: %s", err.c_str());
    if (startFallbackBootstrap()) {
      return;
    }
    fail("Bad config", err.c_str());
    return;
  }

  if (!readJsonStringField(doc, "host_url", hostUrl)) {
    LOG_ERR(MODULE, "host config has no host_url; trying firmware bootstrap fallback");
    if (startFallbackBootstrap()) {
      return;
    }
    fail("Bad config", "host_url is missing");
    return;
  }
  hostUrl = trimTrailingSlash(hostUrl);

  if (!readJsonStringField(doc, "secret", secretHex)) {
    LOG_ERR(MODULE, "host config has no secret; trying firmware bootstrap fallback");
    if (startFallbackBootstrap()) {
      return;
    }
    fail("Bad config", "secret is missing");
    return;
  }
  if (secretHex.size() != 64) {
    LOG_ERR(MODULE, "host config secret has invalid length; trying firmware bootstrap fallback");
    if (startFallbackBootstrap()) {
      return;
    }
    fail("Bad config", "secret must be 32-byte hex");
    return;
  }

  const char* configuredDeviceId = doc["device_id"] | nullptr;
  deviceId = (configuredDeviceId && configuredDeviceId[0] != '\0') ? configuredDeviceId : deviceIdFromMac();
  needsBootstrapPairing = false;

  startWifiThenSync();
}

bool DeviceSyncActivity::loadDebugTemporaryPairingFallback() {
#if BISCUIT_HAS_DEVICE_SYNC_DEBUG_TEMPORARY_PAIRING_FALLBACK
  const std::string fallbackHostUrl = BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_HOST_URL;
  const std::string fallbackSecretHex = BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_SECRET_HEX;
#ifdef BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_DEVICE_ID
  const std::string fallbackDeviceId = BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_DEVICE_ID;
#else
  const std::string fallbackDeviceId;
#endif

  if (fallbackHostUrl.empty()) {
    LOG_ERR(MODULE, "debug temporary fallback host URL is empty");
    return false;
  }
  if (fallbackSecretHex.size() != 64) {
    LOG_ERR(MODULE, "debug temporary fallback secret must be 32-byte hex");
    return false;
  }

  hostUrl = trimTrailingSlash(fallbackHostUrl);
  secretHex.clear();
  bootstrapSecretHex = fallbackSecretHex;
  deviceId = fallbackDeviceId.empty() ? deviceIdFromMac() : fallbackDeviceId;
  needsBootstrapPairing = true;
  statusTitle = "Bootstrap Sync";
  statusDetail = "Using firmware bootstrap key";
  LOG_INF(MODULE, "Using firmware bootstrap config");
  return true;
#else
  return false;
#endif
}

void DeviceSyncActivity::startWifiThenSync() {
  if (WiFi.status() == WL_CONNECTED) {
    startSyncTask(needsBootstrapPairing);
    return;
  }

  state = CONNECTING_WIFI;
  statusTitle = "Connecting WiFi";
  statusDetail = "Using saved WiFi or picker";
  requestUpdate();

  RADIO.ensureWifi();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             fail("WiFi failed", "Sync cancelled");
                             requestUpdate();
                             return;
                           }
                           startSyncTask(needsBootstrapPairing);
                         });
}

void DeviceSyncActivity::startSyncTask(const bool bootstrap) {
  if (syncTaskHandle) {
    LOG_ERR(MODULE, "sync task already running");
    return;
  }
  syncTaskBootstrap = bootstrap;
  const BaseType_t created =
      xTaskCreateUniversal(&DeviceSyncActivity::syncTaskTrampoline, "DeviceSyncTask", SYNC_TASK_STACK_BYTES, this, 1,
                           &syncTaskHandle, ARDUINO_RUNNING_CORE);
  if (created != pdPASS) {
    syncTaskHandle = nullptr;
    fail("Sync failed", "could not start sync task");
    requestUpdate();
  }
}

void DeviceSyncActivity::syncTaskTrampoline(void* param) {
  auto* self = static_cast<DeviceSyncActivity*>(param);
  self->runSyncTask();
  self->syncTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void DeviceSyncActivity::runSyncTask() {
  if (syncTaskBootstrap) {
    performBootstrapThenSync();
  } else {
    performSync();
  }
}

void DeviceSyncActivity::performBootstrapThenSync() {
  {
    powerManager.setPowerSaving(false);
    state = BOOTSTRAPPING;
    statusTitle = "Pairing";
    statusDetail = "Requesting bootstrap challenge...";
    requestUpdateAndWait();

    String challengeUrl = String(hostUrl.c_str()) + "/api/v0/bootstrap/challenge";
    std::string challengeBody = std::string("{\"device_id\":") + quoted(deviceId) + "}";
    String response;
    int code = postJsonWithRetry(challengeUrl, challengeBody, response, 10000);

    if (code != 200) {
      LOG_ERR(MODULE, "bootstrap challenge failed code=%d body=%s", code, response.c_str());
      fail("Pairing failed", std::string("HTTP ") + std::to_string(code));
      requestUpdate();
      return;
    }

    JsonDocument challenge;
    DeserializationError err = deserializeJson(challenge, response);
    if (err || !(challenge["ok"] | false)) {
      LOG_ERR(MODULE, "bootstrap challenge rejected body=%s", response.c_str());
      fail("Pairing failed", err ? err.c_str() : "host rejected bootstrap");
      requestUpdate();
      return;
    }

    const char* nonceValue = challenge["nonce"].as<const char*>();
    if (!nonceValue || nonceValue[0] == '\0') {
      fail("Pairing failed", "missing nonce");
      requestUpdate();
      return;
    }
    const std::string nonce = nonceValue;
    const std::string signature = hmacSha256Hex(bootstrapSecretHex, bootstrapCanonicalMessage(deviceId, nonce));
    if (signature.empty()) {
      fail("Pairing failed", "bad bootstrap key");
      requestUpdate();
      return;
    }

    statusDetail = "Requesting device secret...";
    requestUpdateAndWait();

    std::string bootstrapBody;
    bootstrapBody.reserve(deviceId.size() + nonce.size() + signature.size() + 96);
    bootstrapBody += "{\"auth\":{\"device_id\":";
    bootstrapBody += quoted(deviceId);
    bootstrapBody += ",\"nonce\":";
    bootstrapBody += quoted(nonce);
    bootstrapBody += ",\"signature\":";
    bootstrapBody += quoted(signature);
    bootstrapBody += "}}";

    String bootstrapUrl = String(hostUrl.c_str()) + "/api/v0/bootstrap";
    code = postJsonWithRetry(bootstrapUrl, bootstrapBody, response, 12000);

    if (code != 200) {
      LOG_ERR(MODULE, "bootstrap failed code=%d body=%s", code, response.c_str());
      fail("Pairing failed", std::string("HTTP ") + std::to_string(code));
      requestUpdate();
      return;
    }

    JsonDocument result;
    err = deserializeJson(result, response);
    if (err || !(result["ok"] | false)) {
      LOG_ERR(MODULE, "bootstrap response parse/reject body=%s", response.c_str());
      fail("Pairing failed", err ? err.c_str() : "host rejected pairing");
      requestUpdate();
      return;
    }

    const char* secretValue = result["secret"].as<const char*>();
    if (!secretValue || strlen(secretValue) != 64) {
      fail("Pairing failed", "bad device secret");
      requestUpdate();
      return;
    }
    secretHex = secretValue;
    const char* returnedDeviceId = result["device_id"].as<const char*>();
    if (returnedDeviceId && returnedDeviceId[0] != '\0') {
      deviceId = returnedDeviceId;
    }
    pairedDuringSync = result["created"] | false;

    const std::string config = buildHostConfigJson(hostUrl, deviceId, secretHex);
    if (!Storage.writeFileAtomic(CONFIG_PATH, String(config.c_str()))) {
      fail("Pairing failed", "could not save host.json");
      requestUpdate();
      return;
    }

    needsBootstrapPairing = false;
    bootstrapSecretHex.clear();
    statusDetail = "Pairing saved; syncing...";
    requestUpdateAndWait();
  }

  performSync();
}

bool DeviceSyncActivity::performSyncRound(StudyReviewLog& reviewLog, const uint8_t round, const bool includeStudyLogs) {
  state = SYNCING;
  statusTitle = "Syncing";
  statusDetail = std::string("Round ") + std::to_string(round) + ": requesting challenge...";
  requestUpdateAndWait();

  String challengeUrl = String(hostUrl.c_str()) + "/api/v0/challenge";
  std::string challengeBody = std::string("{\"device_id\":") + quoted(deviceId) + "}";
  String response;
  int code = postJsonWithRetry(challengeUrl, challengeBody, response, 10000);

  if (code != 200) {
    LOG_ERR(MODULE, "challenge failed code=%d body=%s", code, response.c_str());
    fail("Challenge failed", std::string("HTTP ") + std::to_string(code));
    requestUpdate();
    return false;
  }

  JsonDocument challenge;
  DeserializationError err = deserializeJson(challenge, response);
  if (err || !(challenge["ok"] | false)) {
    LOG_ERR(MODULE, "challenge response parse/reject code=%d body=%s", code, response.c_str());
    fail("Challenge failed", err ? err.c_str() : "host rejected device");
    requestUpdate();
    return false;
  }

  const char* nonceValue = challenge["nonce"].as<const char*>();
  if (!nonceValue || nonceValue[0] == '\0') {
    LOG_ERR(MODULE, "challenge response missing nonce body=%s", response.c_str());
    fail("Challenge failed", "missing nonce");
    requestUpdate();
    return false;
  }
  const std::string nonce = nonceValue;

  statusDetail = includeStudyLogs ? "Collecting logs..." : "Scanning packs...";
  requestUpdateAndWait();

  std::vector<StudyReviewLog::SyncSegment> studySegments;
  uint32_t sentStudyLogSeq = 0;
  if (includeStudyLogs) {
    LOG_INF(MODULE, "Sync collect logs: start heap=%lu", static_cast<unsigned long>(esp_get_free_heap_size()));
    if (!reviewLog.collectPendingSegments(studySegments, sentStudyLogSeq, MAX_SYNC_STUDY_LOG_BYTES,
                                          MAX_SYNC_STUDY_LOG_SEGMENTS)) {
      LOG_ERR(MODULE, "could not collect pending study log segments");
    }
    LOG_INF(MODULE, "Sync collect logs: segments=%u bytes=%u sentSeq=%lu heap=%lu",
            static_cast<unsigned>(studySegments.size()), static_cast<unsigned>(studyLogPayloadBytes(studySegments)),
            static_cast<unsigned long>(sentStudyLogSeq), static_cast<unsigned long>(esp_get_free_heap_size()));
  }

  statusDetail = "Scanning packs...";
  requestUpdateAndWait();
  StudyPackReader packReader;
  std::vector<StudyPackInfo> installedPacks;
  LOG_INF(MODULE, "Sync scan packs: start");
  packReader.scanPacks(installedPacks, 64);
  LOG_INF(MODULE, "Sync scan packs: installed=%u heap=%lu", static_cast<unsigned>(installedPacks.size()),
          static_cast<unsigned long>(esp_get_free_heap_size()));

  statusDetail = "Signing payload...";
  requestUpdateAndWait();
  const std::string payload = buildSyncPayload(installedPacks, studySegments);
  LOG_INF(MODULE, "Sync payload built: bytes=%u heap=%lu", static_cast<unsigned>(payload.size()),
          static_cast<unsigned long>(esp_get_free_heap_size()));
  const std::string message = canonicalMessage(deviceId, nonce, payload);
  const std::string signature = hmacSha256Hex(secretHex, message);
  if (signature.empty()) {
    fail("Signing failed", "bad secret or HMAC error");
    requestUpdate();
    return false;
  }

  std::string syncBody;
  syncBody.reserve(payload.size() + 256);
  syncBody += "{\"auth\":{\"device_id\":";
  syncBody += quoted(deviceId);
  syncBody += ",\"nonce\":";
  syncBody += quoted(nonce);
  syncBody += ",\"signature\":";
  syncBody += quoted(signature);
  syncBody += "},\"payload\":";
  syncBody += payload;
  syncBody += "}";
  String syncUrl = String(hostUrl.c_str()) + "/api/v0/sync";
  LOG_INF(MODULE, "Sync POST: bytes=%u url=%s heap=%lu", static_cast<unsigned>(syncBody.size()), syncUrl.c_str(),
          static_cast<unsigned long>(esp_get_free_heap_size()));

  code = postJsonWithRetry(syncUrl, syncBody, response, 15000);
  LOG_INF(MODULE, "Sync POST done: code=%d responseBytes=%u heap=%lu", code, static_cast<unsigned>(response.length()),
          static_cast<unsigned long>(esp_get_free_heap_size()));

  if (code != 200) {
    LOG_ERR(MODULE, "sync failed code=%d body=%s", code, response.c_str());
    fail("Sync failed", std::string("HTTP ") + std::to_string(code));
    requestUpdate();
    return false;
  }

  JsonDocument result;
  err = deserializeJson(result, response);
  if (err || !(result["ok"] | false)) {
    fail("Sync failed", err ? err.c_str() : "host rejected payload");
    requestUpdate();
    return false;
  }

  if (includeStudyLogs) {
    sentStudyLogs += static_cast<int>(studySegments.size());
  }

  const char* hostId = result["host_id"] | nullptr;
  syncedHostId = hostId ? hostId : hostUrl;

  JsonVariant acked = result["acked"];
  if (!acked.isNull()) {
    ackedStatus = ackedStatus || (acked["status"] | false);
    JsonArray notes = acked["notes"].as<JsonArray>();
    if (!notes.isNull()) {
      ackedNotes += static_cast<int>(notes.size());
    }
    JsonArray logs = acked["study_logs"].as<JsonArray>();
    const int roundAckedStudyLogs = logs.isNull() ? 0 : static_cast<int>(logs.size());
    ackedStudyLogs += roundAckedStudyLogs;
    uint32_t roundAckedStudyLogSeq = acked["acked_study_log_seq"] | 0U;
    JsonVariant summary = result["summary"];
    if (!summary.isNull()) {
      JsonVariant accepted = summary["accepted"];
      if (!accepted.isNull()) {
        const uint32_t acceptedSeq = accepted["acked_study_log_seq"] | 0U;
        roundAckedStudyLogSeq = std::max(roundAckedStudyLogSeq, acceptedSeq);
      }
    }
    if (roundAckedStudyLogSeq > 0 && sentStudyLogSeq > 0) {
      const uint32_t seqToMark = std::min(roundAckedStudyLogSeq, sentStudyLogSeq);
      if (!reviewLog.markSynced(seqToMark)) {
        LOG_ERR(MODULE, "could not mark study logs synced through seq=%lu", static_cast<unsigned long>(seqToMark));
      }
    } else if (roundAckedStudyLogs >= static_cast<int>(studySegments.size()) && sentStudyLogSeq > 0) {
      if (!reviewLog.markSynced(sentStudyLogSeq)) {
        LOG_ERR(MODULE, "could not mark study logs synced through seq=%lu", static_cast<unsigned long>(sentStudyLogSeq));
      }
    }
  }

  JsonVariant received = result["received"];
  syncHasMore = false;
  remainingStudyPacks = 0;
  if (!received.isNull()) {
    remainingStudyPacks = received["remaining_study_pack_count"] | 0;
    syncHasMore = received["has_more"] | (remainingStudyPacks > 0);

    JsonArrayConst removePackIds = received["remove_study_pack_ids"].as<JsonArrayConst>();
    if (!removePackIds.isNull()) {
      for (JsonVariantConst item : removePackIds) {
        const char* packId = item | "";
        if (!isValidPackId(packId)) {
          continue;
        }
        if (removeStudyPackById(packId)) {
          removedStudyPacks++;
          LOG_INF(MODULE, "Removed obsolete StudyPack %s", packId);
        } else {
          LOG_ERR(MODULE, "Failed to remove obsolete StudyPack %s", packId);
        }
      }
    }

    JsonArrayConst artifactPacks = received["study_pack_artifacts"].as<JsonArrayConst>();
    if (!artifactPacks.isNull()) {
      LOG_INF(MODULE, "Sync response contains %u StudyPack artifact(s), remaining=%d",
              static_cast<unsigned>(artifactPacks.size()), remainingStudyPacks);
      for (JsonObjectConst pack : artifactPacks) {
        const char* packId = pack["pack_id"] | "";
        const char* revision = pack["revision"] | "";
        statusDetail = std::string("Installing ") + packId;
        requestUpdateAndWait();
        LOG_INF(MODULE, "Installing StudyPack artifact %s rev=%s", packId, revision);
        if (installStudyPackArtifact(pack, hostUrl)) {
          receivedStudyPacks++;
        } else {
          LOG_ERR(MODULE, "Failed to install StudyPack artifact %s", packId);
          fail("Pack install failed", packId);
          requestUpdate();
          return false;
        }
      }
    }

    JsonArrayConst packs = received["study_packs"].as<JsonArrayConst>();
    if (!packs.isNull()) {
      LOG_INF(MODULE, "Sync response contains %u StudyPack(s)", static_cast<unsigned>(packs.size()));
      for (JsonObjectConst pack : packs) {
        const char* packId = pack["pack_id"] | "";
        const char* revision = pack["revision"] | "";
        LOG_INF(MODULE, "Installing received StudyPack %s rev=%s", packId, revision);
        if (installStudyPack(pack)) {
          receivedStudyPacks++;
        } else {
          LOG_ERR(MODULE, "Failed to install received StudyPack %s", packId);
          fail("Pack install failed", packId);
          requestUpdate();
          return false;
        }
      }
    } else {
      receivedStudyPacks += received["study_packs"] | 0;
    }
  }
  requestUpdateAndWait();
  return true;
}

void DeviceSyncActivity::performSync() {
  powerManager.setPowerSaving(false);
  state = SYNCING;
  statusTitle = "Syncing";
  statusDetail = "Starting...";
  requestUpdateAndWait();

  StudyReviewLog reviewLog;
  const uint32_t startedMs = millis();
  bool includeStudyLogs = true;

  for (uint8_t round = 1; round <= MAX_SYNC_ROUNDS; round++) {
    if (!performSyncRound(reviewLog, round, includeStudyLogs)) {
      return;
    }
    includeStudyLogs = false;
    if (!syncHasMore) {
      break;
    }
    if (millis() - startedMs > MAX_SYNC_TOTAL_MS) {
      statusDetail = "More packs remain; run sync again";
      break;
    }
  }

  state = DONE;
  statusTitle = "Sync complete";
  if (remainingStudyPacks > 0) {
    statusDetail = syncedHostId + "\nMore packs queued";
  } else {
    statusDetail = syncedHostId;
  }
  requestUpdate();
}

void DeviceSyncActivity::fail(const std::string& title, const std::string& detail) {
  state = ERROR;
  statusTitle = title;
  statusDetail = detail;
  LOG_ERR(MODULE, "%s: %s", title.c_str(), detail.c_str());
}

void DeviceSyncActivity::loop() {
  if (state == DONE || state == ERROR || state == NEED_CONFIG) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
      return;
    }
  }
}

void DeviceSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Device Sync");

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4;
  renderer.drawCenteredText(UI_12_FONT_ID, y, statusTitle.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 18;

  if (!statusDetail.empty()) {
    const int maxWidth = pageWidth - metrics.contentSidePadding * 2;
    const int maxLines = 6;
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, statusDetail.c_str(), maxWidth, maxLines);
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    }
  }

  if (state == NEED_CONFIG) {
    y += 10;
    renderer.drawCenteredText(SMALL_FONT_ID, y, "Create /biscuit/sync/host.json");
  } else if (state == DONE) {
    y += 6;
    renderer.drawCenteredText(SMALL_FONT_ID, y, ackedStatus ? "Sent: status  Accepted: yes" : "Sent: status  Accepted: no");
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;

    char line[64];
    snprintf(line, sizeof(line), "Notes accepted: %d", ackedNotes);
    renderer.drawCenteredText(SMALL_FONT_ID, y, line);
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;

    snprintf(line, sizeof(line), "Logs sent: %d  accepted: %d", sentStudyLogs, ackedStudyLogs);
    renderer.drawCenteredText(SMALL_FONT_ID, y, line);
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;

    snprintf(line, sizeof(line), "Received packs: %d", receivedStudyPacks);
    renderer.drawCenteredText(SMALL_FONT_ID, y, line);

    if (removedStudyPacks > 0) {
      y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
      snprintf(line, sizeof(line), "Removed old packs: %d", removedStudyPacks);
      renderer.drawCenteredText(SMALL_FONT_ID, y, line);
    }

    if (pairedDuringSync) {
      y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
      renderer.drawCenteredText(SMALL_FONT_ID, y, "Pairing saved to SD");
    }
  }

  const bool canExit = state == DONE || state == ERROR || state == NEED_CONFIG;
  const auto labels = mappedInput.mapLabels(canExit ? "Back" : "", canExit ? "OK" : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
