#include "study/StudyReviewLog.h"

#include <Arduino.h>
#include <HalBoard.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* MODULE = "STUDY";

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
  const uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "%04x%08lx", static_cast<unsigned int>((mac >> 32) & 0xffff),
           static_cast<unsigned long>(mac & 0xffffffffUL));
#if BISCUIT_BOARD_M5PAPER
  return String("m5paper-") + buf;
#else
  return String("xteink-") + buf;
#endif
}

const char* runtimeName() {
#if BISCUIT_BOARD_M5PAPER
  return "m5paper";
#else
  return "default";
#endif
}
}  // namespace

void StudyReviewLog::ensureLoaded() {
  if (loaded) {
    return;
  }
  loaded = true;
  if (!loadState()) {
    state = State{};
  }
}

bool StudyReviewLog::loadState() {
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
  state = next;
  return true;
}

bool StudyReviewLog::saveState() const {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"schema\":1,\"last_seq\":%lu,\"open_segment\":%lu,\"open_event_count\":%lu,\"acked_seq\":%lu}\n",
           static_cast<unsigned long>(state.lastSeq), static_cast<unsigned long>(state.openSegment),
           static_cast<unsigned long>(state.openEventCount), static_cast<unsigned long>(state.ackedSeq));
  return Storage.writeFile(STATE_PATH, String(buf));
}

void StudyReviewLog::segmentPath(char* path, size_t len) const {
  snprintf(path, len, "%s/reviews_%06lu.jsonl", LOG_DIR, static_cast<unsigned long>(state.openSegment));
}

bool StudyReviewLog::rotateIfNeeded() {
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
  if (!rotateIfNeeded()) {
    return false;
  }

  char path[96];
  segmentPath(path, sizeof(path));
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
  if (!rotateIfNeeded()) {
    return false;
  }

  char path[96];
  segmentPath(path, sizeof(path));
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
