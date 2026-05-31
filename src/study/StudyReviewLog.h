#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class StudyReviewLog {
 public:
  struct Event {
    const char* deckId = "";
    const char* cardId = "";
    const char* action = "";
    uint32_t cardIndex = 0;
    uint32_t sessionMs = 0;
  };

  struct PackEvent {
    const char* packId = "";
    const char* packRevision = "";
    const char* episodeId = "";
    const std::vector<std::string>* conceptIds = nullptr;
    const char* action = "";
    const char* choiceId = "";
    uint32_t sessionMs = 0;
    int8_t confidence = -1;
    int8_t effort = -1;
  };

  struct SyncSegment {
    std::string name;
    std::string content;
    uint32_t lastSeq = 0;
  };

  bool append(const Event& event);
  bool appendPackEvent(const PackEvent& event);
  bool collectPendingSegments(std::vector<SyncSegment>& segments, uint32_t& sentThroughSeq,
                              uint32_t maxTotalBytes = 24UL * 1024UL, uint8_t maxSegments = 3);
  bool markSynced(uint32_t ackedSeq);

  static constexpr const char* LOG_DIR = "/biscuit/study/logs/reviews";
  static constexpr const char* STATE_PATH = "/biscuit/study/logs/sync_state.json";

 private:
  struct State {
    uint32_t lastSeq = 0;
    uint32_t openSegment = 1;
    uint32_t openEventCount = 0;
    uint32_t ackedSeq = 0;
  };

  static constexpr uint32_t MAX_SEGMENT_BYTES = 16UL * 1024UL;
  static constexpr uint32_t MAX_SEGMENT_EVENTS = 300;

  State state;
  bool loaded = false;

  void ensureLoaded();
  bool readState(State& out) const;
  bool loadState();
  bool saveState();
  void mergeFreshState();
  bool reconcileStateFromLogs();
  bool rotateIfNeeded();
  void segmentPath(char* path, size_t len) const;
};
