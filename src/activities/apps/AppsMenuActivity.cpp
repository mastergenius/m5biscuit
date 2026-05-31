#include "AppsMenuActivity.h"

#include <HalBoard.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>

#ifndef BISCUIT_MENU_ENABLE_NETWORK_DIAGNOSTICS
#define BISCUIT_MENU_ENABLE_NETWORK_DIAGNOSTICS 1
#endif
#ifndef BISCUIT_MENU_ENABLE_READER_OPDS
#define BISCUIT_MENU_ENABLE_READER_OPDS 1
#endif
#ifndef BISCUIT_MENU_ENABLE_TOOLS_EXTRAS
#define BISCUIT_MENU_ENABLE_TOOLS_EXTRAS 1
#endif

#include "AppCategoryActivity.h"
#include "BatteryMonitorActivity.h"
#if BISCUIT_MENU_ENABLE_TOOLS_EXTRAS
#include "CalculatorActivity.h"
#include "ClockActivity.h"
#include "CountdownActivity.h"
#endif
#if BISCUIT_BOARD_M5PAPER
#include "DeviceSyncActivity.h"
#endif
#include "DeviceInfoActivity.h"
#include "EtchASketchActivity.h"
#if BISCUIT_MENU_ENABLE_NETWORK_DIAGNOSTICS
#include "HostScannerActivity.h"
#include "MdnsBrowserActivity.h"
#include "PingActivity.h"
#endif
#include "MappedInputManager.h"
#include "QrGeneratorActivity.h"
#include "ReadingStatsActivity.h"
#include "StudyActivity.h"
#include "TaskManagerActivity.h"
#include "WifiConnectActivity.h"
#include "WifiScannerActivity.h"
#if BISCUIT_MENU_ENABLE_READER_OPDS
#include "activities/browser/OpdsBookBrowserActivity.h"
#endif
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#if BISCUIT_BOARD_M5PAPER
#include "activities/network/CrossPointWebServerActivity.h"
#else
#include "activities/network/NetworkModeSelectionActivity.h"
#endif
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "components/themes/radar/RadarHomeRenderer.h"
#include "fontIds.h"

namespace {

constexpr int kCategoryCount = 5;

static constexpr RadarNode kRadarNodes[kCategoryCount] = {
    {"READER", 4},
    {"STUDY", 3},
    {"NETWORK", 5},
    {"TOOLS", 5},
    {"SYSTEM", 5},
};

std::unique_ptr<Activity> makeWifiTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
#if BISCUIT_BOARD_M5PAPER
  return std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput);
#else
  return std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput);
#endif
}

std::unique_ptr<Activity> makeCategoryActivity(int index, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  switch (index) {
    case 0: {
      std::vector<AppCategoryActivity::AppEntry> entries = {
          {"Open Book", "Browse and open an ebook", UIIcon::Book,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
          {"Recent Books", "Continue where you left off", UIIcon::Recent,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
          {"Reading Stats", "Pages read and progress", UIIcon::Book,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ReadingStatsActivity>(r, m); }},
#if BISCUIT_MENU_ENABLE_READER_OPDS
          {"OPDS Browser", "Download books from OPDS servers", UIIcon::Library,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
#endif
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(entries), false, index);
    }
    case 1: {
      std::vector<AppCategoryActivity::AppEntry> entries = {
          {"Study Packs", "Offline adaptive learning units", UIIcon::Book,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<StudyActivity>(r, m); }},
#if BISCUIT_BOARD_M5PAPER
          {"Device Sync", "Sync packs, logs, notes and status", UIIcon::Transfer,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceSyncActivity>(r, m); }},
#endif
          {"WiFi Transfer", "Upload and download files", UIIcon::Transfer, makeWifiTransferActivity},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Study", std::move(entries), false, index);
    }
    case 2: {
      std::vector<AppCategoryActivity::AppEntry> entries = {
          {tr(STR_WIFI_CONNECT), "Join a WiFi network", UIIcon::Wifi,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
          {"WiFi Transfer", "Upload and download files", UIIcon::Transfer, makeWifiTransferActivity},
          {tr(STR_WIFI_SCANNER), "APs, signal and channels", UIIcon::Wifi,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
#if BISCUIT_MENU_ENABLE_NETWORK_DIAGNOSTICS
          {tr(STR_HOST_SCANNER), "Find devices on local network", UIIcon::Wifi,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HostScannerActivity>(r, m); }},
          {"mDNS Browser", "Discover local services", UIIcon::Wifi,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MdnsBrowserActivity>(r, m); }},
          {tr(STR_PING_TOOL), "Ping a host or IP address", UIIcon::Wifi,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PingActivity>(r, m); }},
#endif
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Network", std::move(entries), false, index);
    }
    case 3: {
      std::vector<AppCategoryActivity::AppEntry> entries = {
          {tr(STR_ETCH_A_SKETCH), "Draw on the e-ink screen", UIIcon::Image,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EtchASketchActivity>(r, m); }},
          {tr(STR_QR_GENERATOR), "Generate QR codes from text", UIIcon::Image,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
#if BISCUIT_MENU_ENABLE_TOOLS_EXTRAS
          {"Calculator", "Basic calculator", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
          {"Clock", "NTP clock, stopwatch and pomodoro", UIIcon::Recent,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
          {"Countdown", "Big countdown timer", UIIcon::Recent,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CountdownActivity>(r, m); }},
#endif
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Tools", std::move(entries), false, index);
    }
    case 4: {
      std::vector<AppCategoryActivity::AppEntry> entries = {
          {"Settings", "Display, reader, controls and system", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SettingsActivity>(r, m); }},
          {"Device Info", "Chip, flash, RAM and firmware", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceInfoActivity>(r, m); }},
          {"Task Manager", "Heap, uptime and activity stack", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TaskManagerActivity>(r, m); }},
          {"Battery", "Battery level and history graph", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BatteryMonitorActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "System", std::move(entries), false, index);
    }
    default:
      return nullptr;
  }
}

}  // namespace

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  refreshSystemInfo();
  loadLastUsed();
  requestUpdate();
}

void AppsMenuActivity::loop() {
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::RADAR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectorIndex = (selectorIndex + 1) % ITEM_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectorIndex = (selectorIndex - 1 + ITEM_COUNT) % ITEM_COUNT;
      requestUpdate();
    }

    if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
      uint32_t oldHeap = freeHeap;
      bool oldWifi = wifiConnected;
      refreshSystemInfo();
      if ((freeHeap / 1024) != (oldHeap / 1024) || wifiConnected != oldWifi) {
        requestUpdate();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      auto app = makeCategoryActivity(selectorIndex, renderer, mappedInput);
      if (app) activityManager.pushActivity(std::move(app));
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex = (selectorIndex + 1) % ITEM_COUNT;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex = (selectorIndex - 1 + ITEM_COUNT) % ITEM_COUNT;
    requestUpdate();
  }

  if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
    uint32_t oldHeap = freeHeap;
    bool oldWifi = wifiConnected;
    refreshSystemInfo();
    if ((freeHeap / 1024) != (oldHeap / 1024) || wifiConnected != oldWifi) {
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto app = makeCategoryActivity(selectorIndex, renderer, mappedInput);
    if (app) activityManager.pushActivity(std::move(app));
  }
}

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::RADAR) {
    char radioBuf[48];
    char sysBuf[32];
    snprintf(radioBuf, sizeof(radioBuf), "wifi:%s", wifiConnected ? "ON " : "OFF");
    snprintf(sysBuf, sizeof(sysBuf), "heap:%luK", static_cast<unsigned long>(freeHeap / 1024));
    RadarHomeStatus status{radioBuf, sysBuf, static_cast<int>(batteryPercent)};
    RadarHomeRenderer::draw(renderer, kRadarNodes, kCategoryCount, selectorIndex, status);
    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  drawStatusBar();

  constexpr int statusRowY = 42;
  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf), "WiFi: %s | %luK | %s", wifiConnected ? "on" : "off",
           static_cast<unsigned long>(freeHeap / 1024), uptimeStr);
  renderer.drawText(SMALL_FONT_ID, 14, statusRowY, statusBuf);

  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  constexpr int sidePad = 14;
  constexpr int tileGap = 6;
  constexpr int gridTop = statusBarH + 32;
  const int gridBottom = pageHeight - buttonHintsH - 2;
  const int gridHeight = gridBottom - gridTop;

  const int tileW = pageWidth - sidePad * 2;
  const int tileH = (gridHeight - tileGap * (ITEM_COUNT - 1)) / ITEM_COUNT;

  for (int i = 0; i < ITEM_COUNT; i++) {
    const int x = sidePad;
    const int y = gridTop + i * (tileH + tileGap);
    drawTile(i, x, y, tileW, tileH, i == selectorIndex);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");

  renderer.displayBuffer();
}

void AppsMenuActivity::refreshSystemInfo() {
  freeHeap = esp_get_free_heap_size();
  uptimeSeconds = static_cast<unsigned long>(esp_timer_get_time() / 1000000LL);
  batteryPercent = static_cast<uint8_t>(powerManager.getBatteryPercentage());
  wifiConnected = WiFi.status() == WL_CONNECTED;
  lastInfoRefresh = millis();

  unsigned long hrs = uptimeSeconds / 3600;
  unsigned long mins = (uptimeSeconds % 3600) / 60;
  if (hrs > 0) {
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh%02lum", hrs, mins);
  } else {
    snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
  }
}

void AppsMenuActivity::loadLastUsed() {
  for (int i = 0; i < ITEM_COUNT; i++) {
    lastUsedName[i][0] = '\0';
    char path[40];
    snprintf(path, sizeof(path), "/biscuit/lastused_%d.txt", i);
    FsFile file;
    if (Storage.openFileForRead("APPS", path, file)) {
      int len = file.read(reinterpret_cast<uint8_t*>(lastUsedName[i]), sizeof(lastUsedName[i]) - 1);
      if (len > 0) {
        lastUsedName[i][len] = '\0';
        if (lastUsedName[i][len - 1] == '\n') {
          lastUsedName[i][len - 1] = '\0';
        }
      }
      file.close();
    }
  }
}

void AppsMenuActivity::drawStatusBar() const {
  const auto pageWidth = renderer.getScreenWidth();
  constexpr int pad = 14;

  renderer.drawText(UI_12_FONT_ID, pad, 10, "biscuit.", true, EpdFontFamily::BOLD);

  int uptimeW = renderer.getTextWidth(SMALL_FONT_ID, uptimeStr);
  int rightX = pageWidth - pad;
  renderer.drawText(SMALL_FONT_ID, rightX - uptimeW, 14, uptimeStr);
  rightX -= uptimeW + 10;

  char heapStr[16];
  snprintf(heapStr, sizeof(heapStr), "%luK", static_cast<unsigned long>(freeHeap / 1024));
  int heapW = renderer.getTextWidth(SMALL_FONT_ID, heapStr);
  renderer.drawText(SMALL_FONT_ID, rightX - heapW, 14, heapStr);
  rightX -= heapW + 10;

  if (wifiConnected) {
    renderer.fillRect(rightX - 6, 16, 6, 6, true);
  } else {
    renderer.drawRect(rightX - 6, 16, 6, 6, true);
  }
  rightX -= 14;

  GUI.drawBatteryRight(renderer, Rect{rightX - 16, 14, 15, 12});
  renderer.drawLine(pad, 38, pageWidth - pad, 38, true);
}

void AppsMenuActivity::drawTile(int index, int x, int y, int w, int h, bool selected) const {
  if (selected) {
    renderer.fillRect(x, y, w, h, true);
  } else {
    renderer.drawRect(x, y, w, h, true);
  }

  constexpr int pad = 10;
  const char* name = "";
  const char* subtitle = "";
  int appCount = 0;

  switch (index) {
    case 0:
      name = "READER";
      subtitle = "Books, recents, OPDS";
      appCount = 4;
      break;
    case 1:
      name = "STUDY";
      subtitle = "Packs and sync";
      appCount = 3;
      break;
    case 2:
      name = "NETWORK";
      subtitle = "WiFi and diagnostics";
      appCount = 6;
      break;
    case 3:
      name = "TOOLS";
      subtitle = "Small utilities";
      appCount = 5;
      break;
    case 4:
      name = "SYSTEM";
      subtitle = "Settings and debug";
      appCount = 4;
      break;
  }

  int nameY = y + pad;
  renderer.drawText(UI_12_FONT_ID, x + pad, nameY, name, !selected, EpdFontFamily::BOLD);
  nameY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(SMALL_FONT_ID, x + pad, nameY, subtitle, !selected);

  if (lastUsedName[index][0] != '\0') {
    const int lastY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
    renderer.drawText(SMALL_FONT_ID, x + pad, lastY, lastUsedName[index], !selected);
  }

  char countStr[16];
  snprintf(countStr, sizeof(countStr), "%d apps", appCount);
  const int countY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
  const int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
  renderer.drawText(SMALL_FONT_ID, x + w - pad - countW, countY, countStr, !selected);
}
