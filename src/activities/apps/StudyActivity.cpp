#include "StudyActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* actionOrDefault(const char* action) { return action && *action ? action : "good"; }
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
  selectedPackIndex = std::min(selectedPackIndex, static_cast<int>(packs.size()) - 1);
  if (selectedPackIndex < 0) {
    selectedPackIndex = 0;
  }
}

bool StudyActivity::loadSelectedPack() {
  if (packs.empty() || selectedPackIndex < 0 || selectedPackIndex >= static_cast<int>(packs.size())) {
    return false;
  }

  currentPack = packs[selectedPackIndex];
  episodeIndex = 0;
  sessionStartMs = millis();
  if (!currentPack.entryEpisodeId.empty()) {
    return loadEpisodeById(currentPack.entryEpisodeId);
  }
  return loadEpisodeByIndex(0);
}

bool StudyActivity::loadEpisodeById(const std::string& episodeId) {
  if (!reader.loadEpisodeById(currentPack, episodeId, currentEpisode)) {
    errorMessage = "Could not load episode";
    state = ERROR;
    return false;
  }
  if (currentEpisode.actions.empty()) {
    reader.loadRubricActions(currentPack, currentEpisode.rubricId, currentEpisode.actions);
  }
  return true;
}

bool StudyActivity::loadEpisodeByIndex(const int index) {
  if (!reader.loadEpisodeByIndex(currentPack, index, currentEpisode)) {
    return false;
  }
  if (currentEpisode.actions.empty()) {
    reader.loadRubricActions(currentPack, currentEpisode.rubricId, currentEpisode.actions);
  }
  episodeIndex = index;
  return true;
}

void StudyActivity::enterPrompt() {
  state = PROMPT;
  logAction("shown");
  requestUpdate();
}

void StudyActivity::revealCurrent() {
  state = REVEAL;
  logAction("revealed");
  requestUpdate();
}

void StudyActivity::answerCurrent(const char* action, const int8_t confidence, const int8_t effort) {
  logAction(actionOrDefault(action), confidence, effort);
  advanceEpisode();
}

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
  requestUpdate();
}

void StudyActivity::logAction(const char* action, const int8_t confidence, const int8_t effort) {
  if (currentPack.id.empty() || currentEpisode.id.empty()) {
    return;
  }
  reviewLog.appendPackEvent(StudyReviewLog::PackEvent{currentPack.id.c_str(), currentPack.revision.c_str(),
                                                       currentEpisode.id.c_str(), &currentEpisode.conceptIds, action,
                                                       static_cast<uint32_t>(millis() - sessionStartMs), confidence,
                                                       effort});
}

void StudyActivity::loop() {
  if (state == PACK_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedPackIndex = ButtonNavigator::previousIndex(selectedPackIndex, static_cast<int>(packs.size()));
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedPackIndex = ButtonNavigator::nextIndex(selectedPackIndex, static_cast<int>(packs.size()));
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (loadSelectedPack()) {
        enterPrompt();
      } else if (state == ERROR) {
        requestUpdate();
      }
    }
    return;
  }

  if (state == PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
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

  if (state == REVEAL) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
      answerCurrent("again", 1, 5);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      answerCurrent("hard", 2, 4);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      answerCurrent("confused", 1, 4);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      answerCurrent("easy", 5, 1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      answerCurrent("good", 4, 2);
      return;
    }
    return;
  }

  if (state == COMPLETE || state == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PACK_SELECT;
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
                   const auto& pack = packs[i];
                   return pack.title.empty() ? pack.id : pack.title;
                 });
  }
  const auto labels = mappedInput.mapLabels("Back", "Load", "^", "v");
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
    const int promptLines = std::min(5, std::max(2, remainingLines / 2));
    drawWrappedBlock(SMALL_FONT_ID, x, y, width, promptLines, currentEpisode.prompt);
    y += 10;
    drawWrappedBlock(UI_10_FONT_ID, x, y, width, std::max(2, (bottom - y) / std::max(1, lineHeight)),
                     currentEpisode.reveal, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("Quit", "Good", "Again", "Easy");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    drawWrappedBlock(UI_10_FONT_ID, x, y, width, remainingLines, currentEpisode.prompt);
    const auto labels = mappedInput.mapLabels("Back", "Reveal", "", "Reveal");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void StudyActivity::renderComplete() const {
  const int mid = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, mid - 30, "Session complete", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, mid + 10, "Review log saved to SD");
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
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
