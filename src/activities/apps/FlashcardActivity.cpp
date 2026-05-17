#include "FlashcardActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <string>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FlashcardActivity::scanDecks() {
  deckFiles.clear();
  Storage.mkdir(DECK_DIR);
  HalFile dir = Storage.open(DECK_DIR);
  if (!dir) return;
  HalFile entry;
  while ((entry = dir.openNextFile())) {
    char nameBuf[64];
    entry.getName(nameBuf, sizeof(nameBuf));
    std::string name = nameBuf;
    entry.close();
    if (name.size() > 4 && name.substr(name.size() - 4) == ".csv") {
      deckFiles.push_back(std::string(DECK_DIR) + "/" + name);
    }
  }
  dir.close();
}

std::string FlashcardActivity::deckIdFromPath(const std::string& path) {
  const size_t slash = path.rfind('/');
  const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  size_t end = path.rfind('.');
  if (end == std::string::npos || end < start) {
    end = path.size();
  }

  std::string id;
  id.reserve(end - start);
  for (size_t i = start; i < end; i++) {
    const char c = path[i];
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                      c == '_' || c == '.';
    id.push_back(safe ? c : '_');
  }
  return id.empty() ? "deck" : id;
}

bool FlashcardActivity::loadDeck(const std::string& path) {
  cards.clear();
  auto file = Storage.open(path.c_str());
  if (!file) return false;

  char line[300];
  while (file.available()) {
    int len = 0;
    while (file.available() && len < (int)sizeof(line) - 1) {
      char c = (char)file.read();
      if (c == '\n') break;
      line[len++] = c;
    }
    line[len] = '\0';
    if (len == 0) continue;

    // Find comma separator
    char* comma = strchr(line, ',');
    if (!comma) continue;
    *comma = '\0';

    Card card;
    memset(&card, 0, sizeof(card));
    strncpy(card.front, line, sizeof(card.front) - 1);
    strncpy(card.back, comma + 1, sizeof(card.back) - 1);
    cards.push_back(card);
  }
  file.close();
  currentDeckId = deckIdFromPath(path);
  sessionStartMs = millis();
  return !cards.empty();
}

void FlashcardActivity::enterCardFront() {
  state = CARD_FRONT;
  logReviewAction("shown");
  requestUpdate();
}

void FlashcardActivity::logReviewAction(const char* action) {
  if (cards.empty() || cardIndex < 0 || cardIndex >= (int)cards.size()) {
    return;
  }
  char cardId[80];
  snprintf(cardId, sizeof(cardId), "%s:%lu", currentDeckId.c_str(), static_cast<unsigned long>(cardIndex + 1));
  reviewLog.append(StudyReviewLog::Event{currentDeckId.c_str(), cardId, action, static_cast<uint32_t>(cardIndex),
                                         static_cast<uint32_t>(millis() - sessionStartMs)});
}

void FlashcardActivity::onEnter() {
  Activity::onEnter();
  scanDecks();
  state = DECK_SELECT;
  deckIndex = 0;
  cardIndex = 0;
  requestUpdate();
}

void FlashcardActivity::onExit() { Activity::onExit(); }

void FlashcardActivity::loop() {
  if (state == DECK_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { finish(); return; }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      deckIndex = ButtonNavigator::previousIndex(deckIndex, (int)deckFiles.size());
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      deckIndex = ButtonNavigator::nextIndex(deckIndex, (int)deckFiles.size());
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !deckFiles.empty()) {
      if (loadDeck(deckFiles[deckIndex])) {
        cardIndex = 0;
        enterCardFront();
      }
    }
    return;
  }

  if (state == CARD_FRONT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = DECK_SELECT; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      logReviewAction("revealed");
      state = CARD_BACK; requestUpdate();
    }
    return;
  }

  if (state == CARD_BACK) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = DECK_SELECT; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      logReviewAction("again");
      cards[cardIndex].wrong++;
      cardIndex++;
      if (cardIndex >= (int)cards.size()) { state = STATS; requestUpdate(); return; }
      enterCardFront();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      logReviewAction("good");
      cards[cardIndex].correct++;
      cardIndex++;
      if (cardIndex >= (int)cards.size()) { state = STATS; requestUpdate(); return; }
      enterCardFront();
    }
    return;
  }

  if (state == STATS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = DECK_SELECT; requestUpdate();
    }
    return;
  }
}

void FlashcardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Flashcards");

  switch (state) {
    case DECK_SELECT: renderDeckSelect(); break;
    case CARD_FRONT:  renderCardFront();  break;
    case CARD_BACK:   renderCardBack();   break;
    case STATS:       renderStats();      break;
  }
  renderer.displayBuffer();
}

void FlashcardActivity::renderDeckSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight;
  const int listH = pageHeight - listTop - metrics.buttonHintsHeight;

  if (deckFiles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listH / 2, "No decks in /biscuit/flashcards/");
  } else {
    GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH}, (int)deckFiles.size(), deckIndex,
      [this](int i) -> std::string {
        const std::string& p = deckFiles[i];
        size_t slash = p.rfind('/');
        return (slash != std::string::npos) ? p.substr(slash + 1) : p;
      });
  }
  const auto labels = mappedInput.mapLabels("Back", "Load", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderCardFront() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int mid = (metrics.topPadding + metrics.headerHeight + pageHeight - metrics.buttonHintsHeight) / 2;

  char prog[24];
  snprintf(prog, sizeof(prog), "Card %d / %d", cardIndex + 1, (int)cards.size());
  renderer.drawCenteredText(SMALL_FONT_ID, mid - 60, prog);
  renderer.drawCenteredText(UI_12_FONT_ID, mid - 20, cards[cardIndex].front, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("Quit", "Flip", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderCardBack() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int mid = (metrics.topPadding + metrics.headerHeight + pageHeight - metrics.buttonHintsHeight) / 2;

  renderer.drawCenteredText(SMALL_FONT_ID, mid - 60, cards[cardIndex].front);
  renderer.drawCenteredText(UI_12_FONT_ID, mid - 20, cards[cardIndex].back, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("Quit", "", "Wrong", "Correct");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::renderStats() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int top = metrics.topPadding + metrics.headerHeight + 30;

  int total = 0, correct = 0, wrong = 0;
  for (const auto& c : cards) { total++; correct += c.correct; wrong += c.wrong; }
  int pct = (total > 0) ? (correct * 100 / total) : 0;

  renderer.drawCenteredText(UI_12_FONT_ID, top, "Results", true, EpdFontFamily::BOLD);

  char buf[48];
  snprintf(buf, sizeof(buf), "Total: %d", total);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 50, buf);
  snprintf(buf, sizeof(buf), "Correct: %d", correct);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 80, buf);
  snprintf(buf, sizeof(buf), "Wrong: %d", wrong);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 110, buf);
  snprintf(buf, sizeof(buf), "Score: %d%%", pct);
  renderer.drawCenteredText(UI_12_FONT_ID, top + 150, buf, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
