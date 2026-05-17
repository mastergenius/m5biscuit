#pragma once

#include <cstddef>
#include <cstdint>

class StudyReviewLog {
 public:
  struct Event {
    const char* deckId = "";
    const char* cardId = "";
    const char* action = "";
    uint32_t cardIndex = 0;
    uint32_t sessionMs = 0;
  };

  bool append(const Event& event);

  static constexpr const char* LOG_DIR = "/biscuit/study/logs/reviews";
  static constexpr const char* STATE_PATH = "/biscuit/study/logs/sync_state.json";

 private:
  struct State {
    uint32_t lastSeq = 0;
    uint32_t openSegment = 1;
    uint32_t openEventCount = 0;
    uint32_t ackedSeq = 0;
  };

  static constexpr uint32_t MAX_SEGMENT_BYTES = 64UL * 1024UL;
  static constexpr uint32_t MAX_SEGMENT_EVENTS = 1000;

  State state;
  bool loaded = false;

  void ensureLoaded();
  bool loadState();
  bool saveState() const;
  bool rotateIfNeeded();
  void segmentPath(char* path, size_t len) const;
};
