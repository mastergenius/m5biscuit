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
  enum State { PACK_SELECT, PROMPT, CHOICE, REVEAL, COMPLETE, ERROR };

  struct PackProgress {
    bool exists = false;
    bool active = false;
    std::string revision;
    std::string seenRevision;
    std::string completedRevision;
    std::string episodeId;
    std::string stage;
    std::string selectedChoiceId;
    int episodeIndex = 0;
  };

  State state = PACK_SELECT;
  StudyPackReader reader;
  StudyReviewLog reviewLog;
  ButtonNavigator buttonNavigator;
  std::vector<StudyPackInfo> packs;
  std::vector<PackProgress> packProgress;
  StudyPackInfo currentPack;
  StudyEpisode currentEpisode;
  std::string errorMessage;
  int selectedPackIndex = 0;
  int selectedChoiceIndex = 0;
  int episodeIndex = 0;
  unsigned long sessionStartMs = 0;

  void scanPacks();
  bool loadSelectedPack();
  bool loadEpisodeById(const std::string& episodeId);
  bool loadEpisodeByIndex(int index);
  void enterPrompt(bool logShown = true);
  void revealCurrent();
  void returnToCurrentPrompt();
  void chooseCurrent();
  void answerCurrent(const char* action, int8_t confidence, int8_t effort);
  void advanceWithoutAnswer();
  void advanceEpisode();
  bool goToPreviousEpisode();
  void logAction(const char* action, int8_t confidence = -1, int8_t effort = -1, const char* choiceId = "");
  bool loadProgress(int& index, std::string& episodeId, State& savedState, std::string& choiceId) const;
  bool saveProgress(bool active = true) const;
  bool saveCompleted() const;
  PackProgress readProgressForPack(const StudyPackInfo& pack) const;
  std::string progressPath() const;
  std::string progressPathForPackId(const std::string& packId) const;
  std::string packListLabel(int index) const;
  std::string packStatusLabel(const StudyPackInfo& pack, const PackProgress& progress) const;
  int firstActivePackIndex() const;
  bool selectedPackHasActiveProgress() const;
  void restoreSelectedChoice(const std::string& choiceId);
  const StudyAction* findAction(const char* actionId) const;
  const StudyAction* actionAt(size_t index) const;
  const StudyChoice* selectedChoice() const;
  const char* actionLabel(const StudyAction* action, const char* fallback) const;
  std::string actionId(const StudyAction* action, const char* fallback) const;
  void answerAction(const StudyAction* action, const char* fallback, int8_t confidence, int8_t effort);
  bool isChoiceEpisode() const;
  bool usesCompactFeedbackActions() const;

  void renderPackSelect() const;
  void renderChoice() const;
  void renderEpisode(bool reveal) const;
  void renderComplete() const;
  void renderError() const;
  void drawChoiceList(int x, int top, int width, int height) const;
  void drawActionMeanings(int x, int& y, int width, int bottom, int maxItems) const;
  void drawWrappedBlock(int fontId, int x, int& y, int width, int maxLines, const std::string& text,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
};
