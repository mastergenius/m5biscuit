#include "StudyActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
constexpr const char* PROGRESS_DIR = "/biscuit/study/progress";
constexpr size_t PROGRESS_BUF_SIZE = 768;

const char* actionOrDefault(const char* action) { return action && *action ? action : "good"; }

std::string choiceCode(const int index, const std::string& fallback) {
  if (index >= 0 && index < 26) {
    std::string out = "Choice ";
    out.push_back(static_cast<char>('A' + index));
    return out;
  }
  return fallback.empty() ? "Choice" : fallback;
}

}  // namespace

void StudyActivity::onEnter() {
  Activity::onEnter();
  state = PACK_SELECT;
  scanPacks();
  requestUpdate();
}

void StudyActivity::onExit() { Activity::onExit(); }

void StudyActivity::scanPacks() {
  if (!reader.scanPacks(packs)) {
    errorMessage = "Could not scan /biscuit/study/packs";
    state = ERROR;
    return;
  }
  packProgress.clear();
  packProgress.reserve(packs.size());
  for (const auto& pack : packs) {
    packProgress.push_back(readProgressForPack(pack));
  }
  selectedPackIndex = std::min(selectedPackIndex, static_cast<int>(packs.size()) - 1);
  if (selectedPackIndex < 0) {
    selectedPackIndex = 0;
  }
  const int activeIndex = firstActivePackIndex();
  if (activeIndex >= 0) {
    selectedPackIndex = activeIndex;
  }
}

bool StudyActivity::loadSelectedPack() {
  if (packs.empty() || selectedPackIndex < 0 || selectedPackIndex >= static_cast<int>(packs.size())) {
    return false;
  }

  currentPack = packs[selectedPackIndex];
  episodeIndex = 0;
  sessionStartMs = millis();

  int savedIndex = 0;
  std::string savedEpisodeId;
  State savedState = PROMPT;
  std::string savedChoiceId;
  if (loadProgress(savedIndex, savedEpisodeId, savedState, savedChoiceId)) {
    if (!savedEpisodeId.empty() && loadEpisodeById(savedEpisodeId)) {
      episodeIndex = std::max(0, savedIndex);
      restoreSelectedChoice(savedChoiceId);
      state = savedState == REVEAL ? REVEAL : (savedState == CHOICE && isChoiceEpisode() ? CHOICE : PROMPT);
      return true;
    }
    if (loadEpisodeByIndex(savedIndex)) {
      restoreSelectedChoice(savedChoiceId);
      state = savedState == REVEAL ? REVEAL : (savedState == CHOICE && isChoiceEpisode() ? CHOICE : PROMPT);
      return true;
    }
  }

  if (!currentPack.entryEpisodeId.empty()) {
    if (loadEpisodeById(currentPack.entryEpisodeId)) {
      enterPrompt();
      return true;
    }
    return false;
  }
  if (loadEpisodeByIndex(0)) {
    enterPrompt();
    return true;
  }
  return false;
}

bool StudyActivity::loadEpisodeById(const std::string& episodeId) {
  if (!reader.loadEpisodeById(currentPack, episodeId, currentEpisode)) {
    errorMessage = "Could not load episode";
    state = ERROR;
    return false;
  }
  if (currentEpisode.actions.empty()) {
    reader.loadRubricActions(currentPack, currentEpisode.rubricId, currentEpisode.actionIds, currentEpisode.actions);
  }
  selectedChoiceIndex = 0;
  return true;
}

bool StudyActivity::loadEpisodeByIndex(const int index) {
  if (!reader.loadEpisodeByIndex(currentPack, index, currentEpisode)) {
    return false;
  }
  if (currentEpisode.actions.empty()) {
    reader.loadRubricActions(currentPack, currentEpisode.rubricId, currentEpisode.actionIds, currentEpisode.actions);
  }
  episodeIndex = index;
  selectedChoiceIndex = 0;
  return true;
}

void StudyActivity::enterPrompt(const bool logShown) {
  state = isChoiceEpisode() ? CHOICE : PROMPT;
  saveProgress();
  if (logShown) {
    logAction("shown");
  }
  requestUpdate();
}

void StudyActivity::revealCurrent() {
  state = REVEAL;
  saveProgress();
  logAction("revealed");
  requestUpdate();
}

void StudyActivity::returnToCurrentPrompt() {
  state = isChoiceEpisode() ? CHOICE : PROMPT;
  saveProgress();
  requestUpdate();
}

void StudyActivity::chooseCurrent() {
  if (!isChoiceEpisode()) {
    revealCurrent();
    return;
  }
  const int count = static_cast<int>(currentEpisode.choices.size());
  if (count <= 0) {
    revealCurrent();
    return;
  }
  selectedChoiceIndex = std::max(0, std::min(selectedChoiceIndex, count - 1));
  logAction("answered", -1, -1, currentEpisode.choices[selectedChoiceIndex].id.c_str());
  revealCurrent();
}

void StudyActivity::answerCurrent(const char* action, const int8_t confidence, const int8_t effort) {
  logAction(actionOrDefault(action), confidence, effort);
  advanceEpisode();
}

void StudyActivity::advanceWithoutAnswer() { advanceEpisode(); }

void StudyActivity::advanceEpisode() {
  if (!currentEpisode.nextIds.empty()) {
    if (loadEpisodeById(currentEpisode.nextIds.front())) {
      episodeIndex++;
      enterPrompt();
    } else {
      requestUpdate();
    }
    return;
  }

  if (loadEpisodeByIndex(episodeIndex + 1)) {
    enterPrompt();
    return;
  }

  state = COMPLETE;
  saveCompleted();
  requestUpdate();
}

bool StudyActivity::goToPreviousEpisode() {
  if (episodeIndex <= 0) {
    return false;
  }
  if (!loadEpisodeByIndex(episodeIndex - 1)) {
    return false;
  }
  enterPrompt(false);
  return true;
}

void StudyActivity::logAction(const char* action, const int8_t confidence, const int8_t effort, const char* choiceId) {
  if (currentPack.id.empty() || currentEpisode.id.empty()) {
    return;
  }
  reviewLog.appendPackEvent(StudyReviewLog::PackEvent{currentPack.id.c_str(), currentPack.revision.c_str(),
                                                       currentEpisode.id.c_str(), &currentEpisode.conceptIds, action,
                                                       choiceId, static_cast<uint32_t>(millis() - sessionStartMs),
                                                       confidence, effort});
}

std::string StudyActivity::progressPathForPackId(const std::string& packId) const {
  const std::string safeId = StringUtils::sanitizeFilename(packId.empty() ? "pack" : packId, 72);
  return std::string(PROGRESS_DIR) + "/" + safeId + ".json";
}

std::string StudyActivity::progressPath() const { return progressPathForPackId(currentPack.id); }

StudyActivity::PackProgress StudyActivity::readProgressForPack(const StudyPackInfo& pack) const {
  PackProgress progress;
  if (pack.id.empty()) {
    return progress;
  }

  char buf[PROGRESS_BUF_SIZE];
  const std::string path = progressPathForPackId(pack.id);
  const size_t read = Storage.readFileToBuffer(path.c_str(), buf, sizeof(buf), sizeof(buf) - 1);
  if (read == 0) {
    return progress;
  }

  JsonDocument doc;
  if (deserializeJson(doc, buf)) {
    return progress;
  }

  const char* packId = doc["pack_id"] | "";
  if (pack.id != packId) {
    return progress;
  }

  progress.exists = true;
  progress.revision = (doc["revision"] | "");
  progress.seenRevision = (doc["seen_revision"] | "");
  progress.completedRevision = (doc["completed_revision"] | "");
  progress.episodeId = (doc["episode_id"] | "");
  progress.stage = (doc["stage"] | "prompt");
  progress.selectedChoiceId = (doc["selected_choice_id"] | "");
  progress.episodeIndex = doc["episode_index"] | 0;
  if (progress.seenRevision.empty()) {
    progress.seenRevision = progress.revision;
  }

  const bool activeFieldPresent = doc["active"].is<bool>();
  progress.active = doc["active"] | false;
  if (!activeFieldPresent && (!progress.episodeId.empty() || progress.episodeIndex > 0)) {
    progress.active = true;
  }
  if (!pack.revision.empty() && progress.revision != pack.revision) {
    progress.active = false;
  }
  if (progress.episodeIndex < 0) {
    progress.active = false;
    progress.episodeIndex = 0;
  }
  return progress;
}

bool StudyActivity::loadProgress(int& index, std::string& episodeId, State& savedState, std::string& choiceId) const {
  index = 0;
  episodeId.clear();
  savedState = PROMPT;
  choiceId.clear();
  if (currentPack.id.empty()) {
    return false;
  }

  const PackProgress progress = readProgressForPack(currentPack);
  if (!progress.active || (!currentPack.revision.empty() && progress.revision != currentPack.revision)) {
    return false;
  }

  index = progress.episodeIndex;
  episodeId = progress.episodeId;
  choiceId = progress.selectedChoiceId;
  if (progress.stage == "reveal") {
    savedState = REVEAL;
  } else if (progress.stage == "choice") {
    savedState = CHOICE;
  }
  return !episodeId.empty() || index > 0;
}

bool StudyActivity::saveProgress(const bool active) const {
  if (currentPack.id.empty() || currentEpisode.id.empty()) {
    return false;
  }
  if (!Storage.ensureDirectoryExists(PROGRESS_DIR)) {
    return false;
  }

  const PackProgress previous = readProgressForPack(currentPack);
  const char* stage = "prompt";
  if (state == CHOICE) {
    stage = "choice";
  } else if (state == REVEAL) {
    stage = "reveal";
  }

  JsonDocument doc;
  doc["schema"] = "biscuit.study-pack-state.v0";
  doc["pack_id"] = currentPack.id;
  doc["revision"] = currentPack.revision;
  doc["seen_revision"] = currentPack.revision;
  if (!previous.completedRevision.empty()) {
    doc["completed_revision"] = previous.completedRevision;
  }
  doc["active"] = active;
  doc["episode_id"] = currentEpisode.id;
  doc["episode_index"] = episodeIndex;
  doc["stage"] = stage;
  if (isChoiceEpisode()) {
    const StudyChoice* choice = selectedChoice();
    if (choice && !choice->id.empty()) {
      doc["selected_choice_id"] = choice->id;
    }
  }

  String json;
  serializeJson(doc, json);
  json += '\n';
  return Storage.writeFileAtomic(progressPath().c_str(), json);
}

bool StudyActivity::saveCompleted() const {
  if (currentPack.id.empty() || currentEpisode.id.empty()) {
    return false;
  }
  if (!Storage.ensureDirectoryExists(PROGRESS_DIR)) {
    return false;
  }

  JsonDocument doc;
  doc["schema"] = "biscuit.study-pack-state.v0";
  doc["pack_id"] = currentPack.id;
  doc["revision"] = currentPack.revision;
  doc["seen_revision"] = currentPack.revision;
  doc["completed_revision"] = currentPack.revision;
  doc["active"] = false;
  doc["episode_id"] = currentEpisode.id;
  doc["episode_index"] = episodeIndex;
  doc["stage"] = "complete";

  String json;
  serializeJson(doc, json);
  json += '\n';
  return Storage.writeFileAtomic(progressPath().c_str(), json);
}

void StudyActivity::restoreSelectedChoice(const std::string& choiceId) {
  if (choiceId.empty()) {
    selectedChoiceIndex = 0;
    return;
  }
  for (size_t i = 0; i < currentEpisode.choices.size(); ++i) {
    if (currentEpisode.choices[i].id == choiceId) {
      selectedChoiceIndex = static_cast<int>(i);
      return;
    }
  }
  selectedChoiceIndex = 0;
}

std::string StudyActivity::packStatusLabel(const StudyPackInfo& pack, const PackProgress& progress) const {
  if (!progress.exists) {
    return "NEW";
  }
  const bool currentSeen = progress.seenRevision == pack.revision || progress.revision == pack.revision;
  const bool currentDone = progress.completedRevision == pack.revision;
  if (progress.active && progress.revision == pack.revision) {
    const int total = std::max(0, pack.episodeCount);
    char label[24];
    if (total > 0) {
      snprintf(label, sizeof(label), "CONT %d/%d", progress.episodeIndex + 1, total);
    } else {
      snprintf(label, sizeof(label), "CONT %d", progress.episodeIndex + 1);
    }
    return label;
  }
  if (currentDone) {
    return "DONE";
  }
  if (!currentSeen && (!progress.seenRevision.empty() || !progress.completedRevision.empty() || !progress.revision.empty())) {
    return "UPD";
  }
  return "NEW";
}

std::string StudyActivity::packListLabel(const int index) const {
  if (index < 0 || index >= static_cast<int>(packs.size())) {
    return "";
  }
  const auto& pack = packs[index];
  const PackProgress empty;
  const auto& progress = index < static_cast<int>(packProgress.size()) ? packProgress[index] : empty;
  std::string label = packStatusLabel(pack, progress);
  label += "  ";
  label += pack.title.empty() ? pack.id : pack.title;
  return label;
}

int StudyActivity::firstActivePackIndex() const {
  for (size_t i = 0; i < packProgress.size() && i < packs.size(); ++i) {
    if (packProgress[i].active && packProgress[i].revision == packs[i].revision) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool StudyActivity::selectedPackHasActiveProgress() const {
  return selectedPackIndex >= 0 && selectedPackIndex < static_cast<int>(packProgress.size()) &&
         selectedPackIndex < static_cast<int>(packs.size()) && packProgress[selectedPackIndex].active &&
         packProgress[selectedPackIndex].revision == packs[selectedPackIndex].revision;
}

const StudyAction* StudyActivity::findAction(const char* actionId) const {
  if (!actionId || !*actionId) {
    return nullptr;
  }
  for (const auto& action : currentEpisode.actions) {
    if (action.id == actionId) {
      return &action;
    }
  }
  return nullptr;
}

const StudyAction* StudyActivity::actionAt(const size_t index) const {
  return index < currentEpisode.actions.size() ? &currentEpisode.actions[index] : nullptr;
}

const StudyChoice* StudyActivity::selectedChoice() const {
  return selectedChoiceIndex >= 0 && selectedChoiceIndex < static_cast<int>(currentEpisode.choices.size())
             ? &currentEpisode.choices[selectedChoiceIndex]
             : nullptr;
}

const char* StudyActivity::actionLabel(const StudyAction* action, const char* fallback) const {
  if (action && !action->label.empty()) {
    return action->label.c_str();
  }
  return fallback;
}

std::string StudyActivity::actionId(const StudyAction* action, const char* fallback) const {
  if (action && !action->id.empty()) {
    return action->id;
  }
  return actionOrDefault(fallback);
}

void StudyActivity::answerAction(const StudyAction* action, const char* fallback, const int8_t confidence,
                                 const int8_t effort) {
  const std::string id = actionId(action, fallback);
  answerCurrent(id.c_str(), confidence, effort);
}

bool StudyActivity::isChoiceEpisode() const {
  return currentEpisode.responseMode == "choice" || !currentEpisode.choices.empty();
}

bool StudyActivity::usesCompactFeedbackActions() const {
  const bool hasPositive = findAction("good") || findAction("known") || findAction("relevant");
  const bool hasRepair = findAction("confused") || findAction("unclear");
  const bool hasNextSignal = findAction("deeper") || findAction("skipped") || findAction("relevant");
  const bool hasCompact = hasPositive && (hasRepair || hasNextSignal);
  const bool hasSpaced = findAction("again") || findAction("hard") || findAction("easy");
  return hasCompact && !hasSpaced;
}

void StudyActivity::loop() {
  if (state == PACK_SELECT) {
    bool handledNavigation = false;
    buttonNavigator.onNext([this, &handledNavigation] {
      selectedPackIndex = ButtonNavigator::nextIndex(selectedPackIndex, static_cast<int>(packs.size()));
      handledNavigation = true;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, &handledNavigation] {
      selectedPackIndex = ButtonNavigator::previousIndex(selectedPackIndex, static_cast<int>(packs.size()));
      handledNavigation = true;
      requestUpdate();
    });

    if (!handledNavigation && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (loadSelectedPack()) {
        requestUpdate();
      } else if (state == ERROR) {
        requestUpdate();
      }
    }
    return;
  }

  if (state == PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
      if (goToPreviousEpisode()) {
        return;
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
      scanPacks();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      revealCurrent();
    }
    return;
  }

  if (state == CHOICE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
      if (goToPreviousEpisode()) {
        return;
      }
    }
    bool handledNavigation = false;
    buttonNavigator.onNext([this, &handledNavigation] {
      selectedChoiceIndex = ButtonNavigator::nextIndex(selectedChoiceIndex, static_cast<int>(currentEpisode.choices.size()));
      handledNavigation = true;
      saveProgress();
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, &handledNavigation] {
      selectedChoiceIndex =
          ButtonNavigator::previousIndex(selectedChoiceIndex, static_cast<int>(currentEpisode.choices.size()));
      handledNavigation = true;
      saveProgress();
      requestUpdate();
    });
    if (!handledNavigation && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
      scanPacks();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      chooseCurrent();
      return;
    }
    return;
  }

  if (state == REVEAL) {
    if (isChoiceEpisode() || usesCompactFeedbackActions()) {
      const StudyAction* good = findAction("good");
      if (!good) good = findAction("known");
      if (!good) good = findAction("relevant");
      const StudyAction* confused = findAction("unclear");
      if (!confused) confused = findAction("confused");
      const StudyAction* deeper = findAction("deeper");
      if (!deeper) deeper = findAction("relevant");
      if (!deeper) deeper = findAction("skipped");

      if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
        returnToCurrentPrompt();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
          mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (confused) {
          answerAction(confused, "unclear", 1, 4);
        }
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
          mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (deeper) {
          answerAction(deeper, "deeper", 4, 3);
        }
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
        if (good) {
          answerAction(good, "good", 4, 2);
        } else {
          advanceWithoutAnswer();
        }
        return;
      }
      return;
    }

    const StudyAction* again = findAction("again");
    const StudyAction* hard = findAction("hard");
    const StudyAction* confused = findAction("confused");
    const StudyAction* easy = findAction("easy");
    const StudyAction* good = findAction("good");
    if (!again) again = actionAt(0);
    if (!good) good = actionAt(currentEpisode.actions.size() > 1 ? 1 : 0);
    if (!easy) easy = actionAt(currentEpisode.actions.size() > 2 ? 2 : 0);
    if (!hard) hard = good;
    if (!confused) confused = again;

    if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
      answerAction(again, "again", 1, 5);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      answerAction(hard, "hard", 2, 4);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      answerAction(confused, "confused", 1, 4);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      answerAction(easy, "easy", 5, 1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      answerAction(good, "good", 4, 2);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
      returnToCurrentPrompt();
      return;
    }
    return;
  }

  if (state == COMPLETE) {
#if BISCUIT_BOARD_M5PAPER
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      activityManager.goToDeviceSync();
      return;
    }
#endif
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
      scanPacks();
      requestUpdate();
    }
    return;
  }

  if (state == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
      scanPacks();
      requestUpdate();
    }
  }
}

void StudyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Study Packs");

  switch (state) {
    case PACK_SELECT:
      renderPackSelect();
      break;
    case PROMPT:
      renderEpisode(false);
      break;
    case CHOICE:
      renderChoice();
      break;
    case REVEAL:
      renderEpisode(true);
      break;
    case COMPLETE:
      renderComplete();
      break;
    case ERROR:
      renderError();
      break;
  }
  renderer.displayBuffer();
}

void StudyActivity::renderPackSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight;
  const int listH = pageHeight - listTop - metrics.buttonHintsHeight;

  if (packs.empty()) {
    const int x = metrics.contentSidePadding;
    int y = listTop + 40;
    drawWrappedBlock(UI_10_FONT_ID, x, y, pageWidth - x * 2, 4, "No packs in /biscuit/study/packs");
  } else {
    GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH}, static_cast<int>(packs.size()), selectedPackIndex,
                 [this](int i) -> std::string {
                   return packListLabel(i);
                 });
  }
  const auto labels = mappedInput.mapLabels("Back", selectedPackHasActiveProgress() ? "Continue" : "Load", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StudyActivity::renderChoice() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int x = metrics.contentSidePadding;
  const int y0 = metrics.topPadding + metrics.headerHeight + 12;
  int y = y0;
  const int width = pageWidth - x * 2;
  const int bottom = pageHeight - metrics.buttonHintsHeight - 12;

  drawWrappedBlock(UI_10_FONT_ID, x, y, width, 5, currentEpisode.prompt);
  y += 8;

  const int listTop = y;
  const int listH = std::max(64, bottom - listTop);
  drawChoiceList(x, listTop, width, listH);

  const auto labels = mappedInput.mapLabels("Back", "Choose", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StudyActivity::renderEpisode(const bool reveal) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int x = metrics.contentSidePadding;
  const int width = pageWidth - x * 2;
  int y = metrics.topPadding + metrics.headerHeight + 12;

  char progress[64];
  const int total = currentPack.episodeCount > 0 ? currentPack.episodeCount : 0;
  if (total > 0) {
    snprintf(progress, sizeof(progress), "Episode %d / %d", episodeIndex + 1, total);
  } else {
    snprintf(progress, sizeof(progress), "Episode %d", episodeIndex + 1);
  }
  renderer.drawText(SMALL_FONT_ID, x, y, progress);
  y += renderer.getLineHeight(SMALL_FONT_ID) + 8;

  drawWrappedBlock(UI_12_FONT_ID, x, y, width, 2, currentEpisode.title, EpdFontFamily::BOLD);
  y += 8;

  const int bottom = pageHeight - metrics.buttonHintsHeight - 12;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int remainingLines = std::max(3, (bottom - y) / std::max(1, lineHeight));

  if (reveal) {
    const bool compactFeedback = usesCompactFeedbackActions();
    const int promptLines = isChoiceEpisode() ? 0 : std::min(3, std::max(1, remainingLines / 4));
    if (promptLines > 0) {
      drawWrappedBlock(SMALL_FONT_ID, x, y, width, promptLines, currentEpisode.prompt);
      y += 8;
    }

    if (isChoiceEpisode()) {
      const StudyChoice* choice = selectedChoice();
      if (choice) {
        std::string selected = "Selected: ";
        selected += choiceCode(selectedChoiceIndex, choice->id);
        drawWrappedBlock(SMALL_FONT_ID, x, y, width, 1, selected, EpdFontFamily::BOLD);
        y += 6;
      }
    }

    const int meaningReserveLines = currentEpisode.actions.empty() ? 0 : (compactFeedback || isChoiceEpisode() ? 3 : 5);
    const int revealLines = std::max(2, (bottom - y) / std::max(1, lineHeight) - meaningReserveLines);
    drawWrappedBlock(UI_10_FONT_ID, x, y, width, revealLines, currentEpisode.reveal, EpdFontFamily::BOLD);
    y += 6;
    drawActionMeanings(x, y, width, bottom, compactFeedback || isChoiceEpisode() ? 3 : 4);

    const StudyAction* again = findAction("again");
    const StudyAction* good = findAction("good");
    const StudyAction* easy = findAction("easy");
    if (!again) again = actionAt(0);
    if (!good) good = actionAt(currentEpisode.actions.size() > 1 ? 1 : 0);
    if (!easy) easy = actionAt(currentEpisode.actions.size() > 2 ? 2 : 0);
    if (isChoiceEpisode() || compactFeedback) {
      const StudyAction* choiceGood = findAction("good");
      if (!choiceGood) choiceGood = findAction("known");
      if (!choiceGood) choiceGood = findAction("relevant");
      const StudyAction* choiceConfused = findAction("unclear");
      if (!choiceConfused) choiceConfused = findAction("confused");
      const StudyAction* choiceDeeper = findAction("deeper");
      if (!choiceDeeper) choiceDeeper = findAction("relevant");
      if (!choiceDeeper) choiceDeeper = findAction("skipped");
      const auto labels =
          mappedInput.mapLabels("Back", choiceGood ? actionLabel(choiceGood, "Good") : "Next",
                                choiceConfused ? actionLabel(choiceConfused, "Unclear") : "",
                                choiceDeeper ? actionLabel(choiceDeeper, "Deeper") : "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      const auto labels = mappedInput.mapLabels("Back", actionLabel(good, "Good"), actionLabel(again, "Again"),
                                                actionLabel(easy, "Easy"));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else {
    drawWrappedBlock(UI_10_FONT_ID, x, y, width, remainingLines, currentEpisode.prompt);
    const auto labels = mappedInput.mapLabels("Back", "Reveal", episodeIndex > 0 ? "Prev" : "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void StudyActivity::renderComplete() const {
  const int mid = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, mid - 30, "Session complete", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, mid + 10, "Review log saved to SD");
#if BISCUIT_BOARD_M5PAPER
  const auto labels = mappedInput.mapLabels("Back", "Sync", "", "");
#else
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
#endif
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StudyActivity::renderError() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  int y = metrics.topPadding + metrics.headerHeight + 50;
  renderer.drawCenteredText(UI_12_FONT_ID, y, "Study error", true, EpdFontFamily::BOLD);
  y += 50;
  drawWrappedBlock(UI_10_FONT_ID, metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, 5,
                   errorMessage);
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StudyActivity::drawChoiceList(const int x, const int top, const int width, const int height) const {
  const int count = static_cast<int>(currentEpisode.choices.size());
  if (count <= 0 || height <= 0) {
    return;
  }

  constexpr int maxChoiceLines = 3;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowGap = 8;
  const int rowHeight = lineHeight * maxChoiceLines + rowGap;
  const int pageItems = std::max(1, height / std::max(1, rowHeight));
  const int selected = std::max(0, std::min(selectedChoiceIndex, count - 1));
  const int pageStart = (selected / pageItems) * pageItems;
  const int pageEnd = std::min(count, pageStart + pageItems);
  const int textX = x + 8;
  const int textWidth = std::max(16, width - 16);

  for (int i = pageStart; i < pageEnd; ++i) {
    const int slot = i - pageStart;
    const int rowY = top + slot * rowHeight;
    if (rowY >= top + height) {
      break;
    }

    const bool isSelected = i == selected;
    if (isSelected) {
      renderer.fillRoundedRect(x - 4, rowY - 2, width + 8, rowHeight - 2, 4, Color::LightGray);
      renderer.drawRoundedRect(x - 4, rowY - 2, width + 8, rowHeight - 2, 1, 4, true);
    }

    const auto& choice = currentEpisode.choices[i];
    std::string label;
    if (i < 26) {
      label.push_back(static_cast<char>('A' + i));
      label += ". ";
    }
    label += choice.label.empty() ? choice.id : choice.label;

    const auto lines = renderer.wrappedText(UI_10_FONT_ID, label.c_str(), textWidth, maxChoiceLines,
                                            isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    int y = rowY + 3;
    for (const auto& line : lines) {
      renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str(), true,
                        isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      y += lineHeight;
    }
  }

  if (count > pageItems) {
    const int pageCount = (count + pageItems - 1) / pageItems;
    const int pageIndex = selected / pageItems;
    const int barX = x + width + 3;
    const int barTop = top;
    const int barHeight = height;
    const int thumbHeight = std::max(12, (barHeight * pageItems) / count);
    const int thumbY = barTop + ((barHeight - thumbHeight) * pageIndex) / std::max(1, pageCount - 1);
    renderer.drawLine(barX, barTop, barX, barTop + barHeight - 1, true);
    renderer.fillRect(barX - 2, thumbY, 4, thumbHeight, true);
  }
}

void StudyActivity::drawActionMeanings(const int x, int& y, const int width, const int bottom, const int maxItems) const {
  int drawn = 0;
  for (const auto& action : currentEpisode.actions) {
    if (drawn >= maxItems || y >= bottom - renderer.getLineHeight(SMALL_FONT_ID)) {
      return;
    }
    if (action.id == "shown" || action.id == "revealed" || action.id == "answered") {
      continue;
    }
    std::string line = action.label.empty() ? action.id : action.label;
    if (!action.meaning.empty()) {
      line += ": ";
      line += action.meaning;
    }
    drawWrappedBlock(SMALL_FONT_ID, x, y, width, 1, line);
    drawn++;
  }
}

void StudyActivity::drawWrappedBlock(const int fontId, const int x, int& y, const int width, const int maxLines,
                                     const std::string& text, const EpdFontFamily::Style style) const {
  if (text.empty() || maxLines <= 0) {
    return;
  }
  const auto lines = renderer.wrappedText(fontId, text.c_str(), width, maxLines, style);
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& line : lines) {
    renderer.drawText(fontId, x, y, line.c_str(), true, style);
    y += lineHeight;
  }
}
