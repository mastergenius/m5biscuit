#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "study/StudyPackReader.h"
#include "study/StudyReviewLog.h"
#include "util/ButtonNavigator.h"

class StudyActivity final : public Activity {
 public:
  explicit StudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Study", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { PACK_SELECT, PROMPT, REVEAL, COMPLETE, ERROR };

  State state = PACK_SELECT;
  StudyPackReader reader;
  StudyReviewLog reviewLog;
  std::vector<StudyPackInfo> packs;
  StudyPackInfo currentPack;
  StudyEpisode currentEpisode;
  std::string errorMessage;
  int selectedPackIndex = 0;
  int episodeIndex = 0;
  unsigned long sessionStartMs = 0;

  void scanPacks();
  bool loadSelectedPack();
  bool loadEpisodeById(const std::string& episodeId);
  bool loadEpisodeByIndex(int index);
  void enterPrompt();
  void revealCurrent();
  void answerCurrent(const char* action, int8_t confidence, int8_t effort);
  void advanceEpisode();
  void logAction(const char* action, int8_t confidence = -1, int8_t effort = -1);

  void renderPackSelect() const;
  void renderEpisode(bool reveal) const;
  void renderComplete() const;
  void renderError() const;
  void drawWrappedBlock(int fontId, int x, int& y, int width, int maxLines, const std::string& text,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
};
