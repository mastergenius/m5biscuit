#include "study/StudyReviewLog.h"

#include <Arduino.h>
#include <HalBoard.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* MODULE = "STUDY";
constexpr uint8_t MAX_COLLECT_SEGMENT_FILES_SCANNED = 8;
constexpr uint32_t MAX_COLLECT_FILE_SCAN_BYTES = 32UL * 1024UL;
constexpr uint32_t MAX_COLLECT_LINE_BYTES = 2UL * 1024UL;
constexpr uint32_t MAX_COLLECT_DURATION_MS = 4000;
constexpr uint32_t SYNCED_CORRUPT_LINE_EARLY_SKIP_BYTES = 512;
constexpr const char* REVIEW_EVENT_PREFIX = "{\"schema\":\"biscuit.review-event.v0\"";

bool parseReviewSegmentNo(const std::string& name, uint32_t& segmentNo) {
  if (name.rfind("reviews_", 0) != 0 || name.size() != strlen("reviews_000001.jsonl")) {
    return false;
  }
  if (name.compare(name.size() - 6, 6, ".jsonl") != 0) {
    return false;
  }
  const std::string digits = name.substr(strlen("reviews_"), 6);
  char* end = nullptr;
  const unsigned long parsed = strtoul(digits.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed == 0) {
    return false;
  }
  segmentNo = static_cast<uint32_t>(parsed);
  return true;
}

bool readUintField(const char* json, const char* key, uint32_t& value) {
  const char* pos = strstr(json, key);
  if (!pos) {
    return false;
  }
  pos += strlen(key);
  while (*pos == ' ' || *pos == ':') {
    pos++;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(pos, &end, 10);
  if (end == pos) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool trimToEmbeddedReviewEvent(std::string& line) {
  const size_t embedded = line.find(REVIEW_EVENT_PREFIX, 1);
  if (embedded == std::string::npos) {
    return false;
  }
  line.erase(0, embedded);
  return true;
}

void writeJsonString(HalFile& file, const char* value) {
  file.write('"');
  if (value) {
    for (const char* p = value; *p; p++) {
      switch (*p) {
        case '"': file.print("\\\""); break;
        case '\\': file.print("\\\\"); break;
        case '\b': file.print("\\b"); break;
        case '\f': file.print("\\f"); break;
        case '\n': file.print("\\n"); break;
        case '\r': file.print("\\r"); break;
        case '\t': file.print("\\t"); break;
        default:
          if (static_cast<unsigned char>(*p) < 0x20) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned char>(*p));
            file.print(esc);
          } else {
            file.write(static_cast<uint8_t>(*p));
          }
          break;
      }
    }
  }
  file.write('"');
}

String deviceId() {
  uint8_t efuseMac[6] = {};
  String mac;
  if (esp_efuse_mac_get_default(efuseMac) == ESP_OK) {
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", efuseMac[0], efuseMac[1], efuseMac[2], efuseMac[3],
             efuseMac[4], efuseMac[5]);
    mac = buf;
  } else {
    mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    if (mac.isEmpty() || mac == "000000000000") {
      mac = "unknown";
    }
  }
#if BISCUIT_BOARD_M5PAPER
  return String("m5paper-") + mac;
#else
  return String("xteink-") + mac;
#endif
}

const char* runtimeName() {
#if BISCUIT_BOARD_M5PAPER
  return "m5paper";
#else
  return "default";
#endif
}

void reviewLogPath(char* path, const size_t len, const uint32_t segmentNo) {
  snprintf(path, len, "%s/reviews_%06lu.jsonl", StudyReviewLog::LOG_DIR, static_cast<unsigned long>(segmentNo));
}

std::string fileNameFromPath(const char* name) {
  const std::string path = name ? name : "";
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ensureTrailingNewline(const char* path) {
  HalFile file = Storage.open(path);
  if (!file) {
    return true;
  }
  const size_t size = file.fileSize();
  if (size == 0) {
    file.close();
    return true;
  }
  if (!file.seek(size - 1)) {
    file.close();
    return false;
  }
  const int last = file.read();
  file.close();
  if (last == '\n') {
    return true;
  }

  file = Storage.open(path, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    return false;
  }
  file.write('\n');
  file.flush();
  file.close();
  return true;
}
}  // namespace

void StudyReviewLog::ensureLoaded() {
  if (loaded) {
    return;
  }
  loaded = true;
  if (!loadState()) {
    state = State{};
    state.openSegment = 1;
  }
}

bool StudyReviewLog::readState(State& out) const {
  char buf[256];
  const size_t read = Storage.readFileToBuffer(STATE_PATH, buf, sizeof(buf), sizeof(buf) - 1);
  if (read == 0) {
    return false;
  }

  State next;
  readUintField(buf, "\"last_seq\"", next.lastSeq);
  readUintField(buf, "\"open_segment\"", next.openSegment);
  readUintField(buf, "\"open_event_count\"", next.openEventCount);
  readUintField(buf, "\"acked_seq\"", next.ackedSeq);
  if (next.openSegment == 0) {
    next.openSegment = 1;
  }
  if (next.ackedSeq > next.lastSeq) {
    next.ackedSeq = next.lastSeq;
  }
  out = next;
  return true;
}

bool StudyReviewLog::loadState() {
  State next;
  if (!readState(next)) {
    return false;
  }
  state = next;
  return true;
}

void StudyReviewLog::mergeFreshState() {
  State disk;
  if (!readState(disk)) {
    return;
  }

  state.lastSeq = std::max(state.lastSeq, disk.lastSeq);
  state.ackedSeq = std::max(state.ackedSeq, disk.ackedSeq);
  if (disk.openSegment > state.openSegment) {
    state.openSegment = disk.openSegment;
    state.openEventCount = disk.openEventCount;
  } else if (disk.openSegment == state.openSegment) {
    state.openEventCount = std::max(state.openEventCount, disk.openEventCount);
  }
  if (state.openSegment == 0) {
    state.openSegment = 1;
  }
  if (state.ackedSeq > state.lastSeq) {
    state.ackedSeq = state.lastSeq;
  }
}

bool StudyReviewLog::saveState() {
  mergeFreshState();
  if (state.openSegment == 0) {
    state.openSegment = 1;
  }
  if (state.ackedSeq > state.lastSeq) {
    state.ackedSeq = state.lastSeq;
  }
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"schema\":1,\"last_seq\":%lu,\"open_segment\":%lu,\"open_event_count\":%lu,\"acked_seq\":%lu}\n",
           static_cast<unsigned long>(state.lastSeq), static_cast<unsigned long>(state.openSegment),
           static_cast<unsigned long>(state.openEventCount), static_cast<unsigned long>(state.ackedSeq));
  return Storage.writeFileAtomic(STATE_PATH, String(buf));
}

bool StudyReviewLog::reconcileStateFromLogs() {
  if (!Storage.exists(LOG_DIR)) {
    return true;
  }

  uint32_t maxSegment = state.openSegment > 0 ? state.openSegment : 1;
  HalFile root = Storage.open(LOG_DIR);
  if (root && root.isDirectory()) {
    HalFile entry;
    while ((entry = root.openNextFile())) {
      char nameBuf[128] = {};
      entry.getName(nameBuf, sizeof(nameBuf));
      const std::string name = fileNameFromPath(nameBuf);
      uint32_t segmentNo = 0;
      if (!entry.isDirectory() && parseReviewSegmentNo(name, segmentNo)) {
        maxSegment = std::max(maxSegment, segmentNo);
      }
      entry.close();
    }
    root.close();
  }

  uint32_t observedLastSeq = 0;
  uint32_t observedOpenEventCount = 0;
  const uint32_t firstSegment =
      maxSegment > MAX_COLLECT_SEGMENT_FILES_SCANNED ? maxSegment - MAX_COLLECT_SEGMENT_FILES_SCANNED + 1 : 1;

  for (uint32_t segmentNo = firstSegment; segmentNo <= maxSegment; segmentNo++) {
    char path[96];
    reviewLogPath(path, sizeof(path), segmentNo);
    HalFile file = Storage.open(path);
    if (!file || file.isDirectory()) {
      if (file) {
        file.close();
      }
      continue;
    }

    std::string line;
    line.reserve(256);
    uint32_t scannedBytes = 0;
    auto consumeLine = [&]() {
      if (line.empty()) {
        return;
      }
      uint32_t seq = 0;
      if (readUintField(line.c_str(), "\"seq\"", seq)) {
        observedLastSeq = std::max(observedLastSeq, seq);
        if (segmentNo == maxSegment) {
          observedOpenEventCount++;
        }
      }
      line.clear();
    };

    while (file.available() && scannedBytes < MAX_COLLECT_FILE_SCAN_BYTES) {
      const int c = file.read();
      if (c < 0 || c == '\r') {
        continue;
      }
      scannedBytes++;
      line.push_back(static_cast<char>(c));
      if (line.size() > MAX_COLLECT_LINE_BYTES) {
        line.clear();
        while (file.available()) {
          const int skipped = file.read();
          scannedBytes++;
          if (skipped == '\n' || scannedBytes >= MAX_COLLECT_FILE_SCAN_BYTES) {
            break;
          }
        }
        continue;
      }
      if (c == '\n') {
        consumeLine();
      }
    }
    consumeLine();
    file.close();
  }

  bool changed = false;
  if (maxSegment > state.openSegment) {
    state.openSegment = maxSegment;
    changed = true;
  }
  if (observedLastSeq > state.lastSeq) {
    LOG_INF(MODULE, "Reconciled review state last_seq from %lu to %lu",
            static_cast<unsigned long>(state.lastSeq), static_cast<unsigned long>(observedLastSeq));
    state.lastSeq = observedLastSeq;
    changed = true;
  }
  if (observedOpenEventCount > state.openEventCount && state.openSegment == maxSegment) {
    state.openEventCount = observedOpenEventCount;
    changed = true;
  }
  if (state.ackedSeq > state.lastSeq) {
    state.ackedSeq = state.lastSeq;
    changed = true;
  }
  return !changed || saveState();
}

bool StudyReviewLog::collectPendingSegments(std::vector<SyncSegment>& segments, uint32_t& sentThroughSeq,
                                            const uint32_t maxTotalBytes, const uint8_t maxSegments) {
  ensureLoaded();
  mergeFreshState();
  if (state.ackedSeq >= state.lastSeq) {
    reconcileStateFromLogs();
  }
  segments.clear();
  sentThroughSeq = state.ackedSeq;
  if (state.ackedSeq >= state.lastSeq || maxTotalBytes == 0 || maxSegments == 0) {
    if (state.ackedSeq >= state.lastSeq && maxTotalBytes > 0 && maxSegments > 0 && Storage.exists(LOG_DIR)) {
      char path[96];
      reviewLogPath(path, sizeof(path), state.openSegment);
      HalFile file = Storage.open(path);
      if (file && !file.isDirectory()) {
        SyncSegment segment;
        char segmentName[32];
        snprintf(segmentName, sizeof(segmentName), "reviews_%06lu.jsonl", static_cast<unsigned long>(state.openSegment));
        segment.name = segmentName;
        std::string line;
        line.reserve(256);
        uint32_t totalBytes = 0;
        auto consumeLine = [&]() {
          if (line.empty()) {
            return;
          }
          uint32_t seq = 0;
          if (readUintField(line.c_str(), "\"seq\"", seq) && totalBytes + line.size() <= maxTotalBytes) {
            segment.content += line;
            segment.lastSeq = std::max(segment.lastSeq, seq);
            totalBytes += static_cast<uint32_t>(line.size());
          }
          line.clear();
        };
        while (file.available() && totalBytes < maxTotalBytes) {
          const int c = file.read();
          if (c < 0 || c == '\r') {
            continue;
          }
          line.push_back(static_cast<char>(c));
          if (line.size() > MAX_COLLECT_LINE_BYTES) {
            line.clear();
            while (file.available()) {
              const int skipped = file.read();
              if (skipped == '\n') {
                break;
              }
            }
            continue;
          }
          if (c == '\n') {
            consumeLine();
          }
        }
        consumeLine();
        file.close();
        if (!segment.content.empty()) {
          sentThroughSeq = std::max(sentThroughSeq, segment.lastSeq);
          segments.push_back(std::move(segment));
          LOG_INF(MODULE, "Safety replay review segment: %s bytes=%lu lastSeq=%lu", segmentName,
                  static_cast<unsigned long>(totalBytes), static_cast<unsigned long>(sentThroughSeq));
        }
      } else if (file) {
        file.close();
      }
    }
    return true;
  }

  if (!Storage.exists(LOG_DIR)) {
    return true;
  }

  uint32_t totalBytes = 0;
  bool budgetFull = false;
  const uint32_t ackedSeq = state.ackedSeq;
  const uint32_t firstSegment =
      state.openSegment > MAX_COLLECT_SEGMENT_FILES_SCANNED ? state.openSegment - MAX_COLLECT_SEGMENT_FILES_SCANNED + 1 : 1;
  uint8_t scannedFiles = 0;

  LOG_INF(MODULE, "Collect pending reviews: last=%lu acked=%lu openSegment=%lu firstSegment=%lu",
          static_cast<unsigned long>(state.lastSeq), static_cast<unsigned long>(state.ackedSeq),
          static_cast<unsigned long>(state.openSegment), static_cast<unsigned long>(firstSegment));

  const uint32_t startedMs = millis();
  uint32_t segmentOrder[MAX_COLLECT_SEGMENT_FILES_SCANNED] = {};
  uint8_t segmentOrderCount = 0;
  const bool smallBacklog = state.lastSeq >= state.ackedSeq && (state.lastSeq - state.ackedSeq) <= MAX_SEGMENT_EVENTS;
  if (smallBacklog) {
    for (uint32_t segmentNo = state.openSegment; segmentNo >= firstSegment && segmentOrderCount < MAX_COLLECT_SEGMENT_FILES_SCANNED;
         segmentNo--) {
      segmentOrder[segmentOrderCount++] = segmentNo;
      if (segmentNo == 0) {
        break;
      }
    }
  } else {
    for (uint32_t segmentNo = firstSegment; segmentNo <= state.openSegment &&
                                           segmentOrderCount < MAX_COLLECT_SEGMENT_FILES_SCANNED;
         segmentNo++) {
      segmentOrder[segmentOrderCount++] = segmentNo;
    }
  }

  for (uint8_t orderIndex = 0; orderIndex < segmentOrderCount; orderIndex++) {
    if (budgetFull || segments.size() >= maxSegments || scannedFiles >= MAX_COLLECT_SEGMENT_FILES_SCANNED) {
      break;
    }

    const uint32_t segmentNo = segmentOrder[orderIndex];
    char path[96];
    reviewLogPath(path, sizeof(path), segmentNo);
    HalFile file = Storage.open(path);
    if (!file || file.isDirectory()) {
      if (file) {
        file.close();
      }
      continue;
    }
    scannedFiles++;

    SyncSegment segment;
    char segmentName[32];
    snprintf(segmentName, sizeof(segmentName), "reviews_%06lu.jsonl", static_cast<unsigned long>(segmentNo));
    LOG_INF(MODULE, "Collect pending reviews scanning %s", segmentName);
    segment.name = segmentName;
    std::string line;
    line.reserve(256);
    bool skippingLongLine = false;
    bool blockedByCorruptPending = false;
    uint32_t scannedBytes = 0;

    auto consumeLine = [&]() {
      if (line.empty()) {
        return;
      }
      if (line.back() != '\n' || line.size() < 3 || line.front() != '{' || line[line.size() - 2] != '}') {
        uint32_t seq = 0;
        const bool hasSeq = readUintField(line.c_str(), "\"seq\"", seq);
        if (trimToEmbeddedReviewEvent(line) && line.back() == '\n' && line.size() >= 3 && line.front() == '{' &&
            line[line.size() - 2] == '}') {
          LOG_ERR(MODULE, "Recovered embedded review event in %s", path);
        } else {
          if (hasSeq && seq <= ackedSeq) {
            LOG_ERR(MODULE, "Skipping corrupt already-acked review log line in %s", path);
          } else {
            LOG_ERR(MODULE, "Stopping pending review collection at corrupt line in %s", path);
            blockedByCorruptPending = true;
          }
          line.clear();
          return;
        }
      }
      uint32_t seq = 0;
      if (!readUintField(line.c_str(), "\"seq\"", seq) || seq <= ackedSeq) {
        line.clear();
        return;
      }
      if (totalBytes + line.size() > maxTotalBytes) {
        budgetFull = true;
        line.clear();
        return;
      }
      segment.content += line;
      segment.lastSeq = std::max(segment.lastSeq, seq);
      totalBytes += static_cast<uint32_t>(line.size());
      line.clear();
    };

    while (!budgetFull && !blockedByCorruptPending && file.available() && scannedBytes < MAX_COLLECT_FILE_SCAN_BYTES &&
           millis() - startedMs < MAX_COLLECT_DURATION_MS) {
      const int c = file.read();
      if (c < 0 || c == '\r') {
        continue;
      }
      scannedBytes++;
      if (skippingLongLine) {
        if (c == '\n') {
          skippingLongLine = false;
          line.clear();
        }
        continue;
      }
      line.push_back(static_cast<char>(c));
      if (line.size() > MAX_COLLECT_LINE_BYTES) {
        if (trimToEmbeddedReviewEvent(line) && line.size() <= MAX_COLLECT_LINE_BYTES) {
          LOG_ERR(MODULE, "Recovered embedded review event after long corrupt prefix in %s", path);
          continue;
        }
        uint32_t seq = 0;
        const bool hasSeq = readUintField(line.c_str(), "\"seq\"", seq);
        if (hasSeq && seq <= ackedSeq) {
          LOG_ERR(MODULE, "Review log line too long but already acked in %s, skipping", path);
        } else {
          LOG_ERR(MODULE, "Review log line too long in %s, blocking collection", path);
          blockedByCorruptPending = true;
        }
        line.clear();
        skippingLongLine = true;
        continue;
      }
      if (line.size() >= SYNCED_CORRUPT_LINE_EARLY_SKIP_BYTES) {
        uint32_t seq = 0;
        if (readUintField(line.c_str(), "\"seq\"", seq) && seq <= ackedSeq &&
            line.find(REVIEW_EVENT_PREFIX, 1) == std::string::npos) {
          LOG_ERR(MODULE, "Skipping synced corrupt review log prefix in %s", path);
          line.clear();
          skippingLongLine = true;
          continue;
        }
      }
      if (c == '\n') {
        consumeLine();
      }
      if ((line.size() % 512) == 0) {
        yield();
      }
    }
    consumeLine();
    file.close();

    if (!segment.content.empty()) {
      sentThroughSeq = std::max(sentThroughSeq, segment.lastSeq);
      segments.push_back(std::move(segment));
    }
    if (blockedByCorruptPending) {
      break;
    }
    if (millis() - startedMs >= MAX_COLLECT_DURATION_MS) {
      LOG_ERR(MODULE, "Collect pending reviews timed out after %lu ms", static_cast<unsigned long>(millis() - startedMs));
      break;
    }
    yield();
  }

  LOG_INF(MODULE, "Collect pending reviews done: segments=%u bytes=%lu scannedFiles=%u",
          static_cast<unsigned>(segments.size()), static_cast<unsigned long>(totalBytes), static_cast<unsigned>(scannedFiles));

  return true;
}

bool StudyReviewLog::markSynced(uint32_t ackedSeq) {
  ensureLoaded();
  mergeFreshState();
  if (ackedSeq > state.lastSeq) {
    ackedSeq = state.lastSeq;
  }
  if (ackedSeq <= state.ackedSeq) {
    return true;
  }
  state.ackedSeq = ackedSeq;
  return saveState();
}

void StudyReviewLog::segmentPath(char* path, size_t len) const {
  snprintf(path, len, "%s/reviews_%06lu.jsonl", LOG_DIR, static_cast<unsigned long>(state.openSegment));
}

bool StudyReviewLog::rotateIfNeeded() {
  mergeFreshState();
  if (!Storage.ensureDirectoryExists(LOG_DIR)) {
    LOG_ERR(MODULE, "Could not create review log dir: %s", LOG_DIR);
    return false;
  }

  char path[96];
  segmentPath(path, sizeof(path));
  HalFile file = Storage.open(path);
  const uint32_t size = file ? static_cast<uint32_t>(file.fileSize()) : 0;
  if (file) {
    file.close();
  }
  if (state.openEventCount < MAX_SEGMENT_EVENTS && size < MAX_SEGMENT_BYTES) {
    return true;
  }

  state.openSegment++;
  state.openEventCount = 0;
  return saveState();
}

bool StudyReviewLog::append(const Event& event) {
  ensureLoaded();
  mergeFreshState();
  if (!rotateIfNeeded()) {
    return false;
  }

  char path[96];
  segmentPath(path, sizeof(path));
  if (!ensureTrailingNewline(path)) {
    LOG_ERR(MODULE, "Could not repair review log segment boundary: %s", path);
    return false;
  }
  HalFile file = Storage.open(path, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR(MODULE, "Could not open review log segment: %s", path);
    return false;
  }

  const uint32_t seq = state.lastSeq + 1;
  file.print("{\"v\":1,\"seq\":");
  file.print(static_cast<unsigned long>(seq));
  file.print(",\"segment\":");
  file.print(static_cast<unsigned long>(state.openSegment));
  file.print(",\"device_id\":");
  const String id = deviceId();
  writeJsonString(file, id.c_str());
  file.print(",\"deck_id\":");
  writeJsonString(file, event.deckId);
  file.print(",\"card_id\":");
  writeJsonString(file, event.cardId);
  file.print(",\"card_index\":");
  file.print(static_cast<unsigned long>(event.cardIndex));
  file.print(",\"action\":");
  writeJsonString(file, event.action);
  file.print(",\"session_ms\":");
  file.print(static_cast<unsigned long>(event.sessionMs));
  file.print(",\"clock\":\"unknown\"}\n");
  file.flush();
  file.close();

  state.lastSeq = seq;
  state.openEventCount++;
  if (!saveState()) {
    LOG_ERR(MODULE, "Could not save review sync state");
    return false;
  }
  return true;
}

bool StudyReviewLog::appendPackEvent(const PackEvent& event) {
  ensureLoaded();
  mergeFreshState();
  if (!rotateIfNeeded()) {
    return false;
  }

  char path[96];
  segmentPath(path, sizeof(path));
  if (!ensureTrailingNewline(path)) {
    LOG_ERR(MODULE, "Could not repair review log segment boundary: %s", path);
    return false;
  }
  HalFile file = Storage.open(path, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR(MODULE, "Could not open review log segment: %s", path);
    return false;
  }

  const uint32_t seq = state.lastSeq + 1;
  file.print("{\"schema\":\"biscuit.review-event.v0\",\"seq\":");
  file.print(static_cast<unsigned long>(seq));
  file.print(",\"segment\":");
  file.print(static_cast<unsigned long>(state.openSegment));
  file.print(",\"device_id\":");
  const String id = deviceId();
  writeJsonString(file, id.c_str());
  file.print(",\"runtime\":");
  writeJsonString(file, runtimeName());
  file.print(",\"pack_id\":");
  writeJsonString(file, event.packId);
  if (event.packRevision && *event.packRevision) {
    file.print(",\"pack_revision\":");
    writeJsonString(file, event.packRevision);
  }
  file.print(",\"episode_id\":");
  writeJsonString(file, event.episodeId);
  if (event.conceptIds) {
    file.print(",\"concept_ids\":[");
    for (size_t i = 0; i < event.conceptIds->size(); i++) {
      if (i > 0) {
        file.write(',');
      }
      writeJsonString(file, (*event.conceptIds)[i].c_str());
    }
    file.write(']');
  }
  file.print(",\"action\":");
  writeJsonString(file, event.action);
  if (event.choiceId && *event.choiceId) {
    file.print(",\"response\":{\"choice_id\":");
    writeJsonString(file, event.choiceId);
    file.write('}');
  }
  file.print(",\"session_ms\":");
  file.print(static_cast<unsigned long>(event.sessionMs));
  if (event.confidence >= 0) {
    file.print(",\"confidence\":");
    file.print(static_cast<int>(event.confidence));
  }
  if (event.effort >= 0) {
    file.print(",\"effort\":");
    file.print(static_cast<int>(event.effort));
  }
  file.print(",\"clock\":\"unknown\"}\n");
  file.flush();
  file.close();

  state.lastSeq = seq;
  state.openEventCount++;
  if (!saveState()) {
    LOG_ERR(MODULE, "Could not save review sync state");
    return false;
  }
  return true;
}
