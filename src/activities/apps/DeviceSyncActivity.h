#pragma once

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "activities/Activity.h"

class StudyReviewLog;

class DeviceSyncActivity final : public Activity {
 public:
  explicit DeviceSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == SYNCING || state == BOOTSTRAPPING || state == CONNECTING_WIFI; }

 private:
  enum State {
    LOADING_CONFIG,
    NEED_CONFIG,
    CONNECTING_WIFI,
    BOOTSTRAPPING,
    SYNCING,
    DONE,
    ERROR,
  };

  static constexpr const char* CONFIG_PATH = "/biscuit/sync/host.json";

  State state = LOADING_CONFIG;
  std::string hostUrl;
  std::string deviceId;
  std::string secretHex;
  std::string bootstrapSecretHex;
  std::string statusTitle;
  std::string statusDetail;
  std::string syncedHostId;
  bool ackedStatus = false;
  int ackedNotes = 0;
  int sentStudyLogs = 0;
  int ackedStudyLogs = 0;
  int receivedStudyPacks = 0;
  int removedStudyPacks = 0;
  int remainingStudyPacks = 0;
  bool pairedDuringSync = false;
  bool needsBootstrapPairing = false;
  bool syncHasMore = false;
  bool syncTaskBootstrap = false;
  TaskHandle_t syncTaskHandle = nullptr;

  void loadConfigAndStart();
  bool loadDebugTemporaryPairingFallback();
  void startWifiThenSync();
  void startSyncTask(bool bootstrap);
  static void syncTaskTrampoline(void* param);
  void runSyncTask();
  void performBootstrapThenSync();
  void performSync();
  bool performSyncRound(StudyReviewLog& reviewLog, uint8_t round, bool includeStudyLogs);
  void resetResultStats();
  void fail(const std::string& title, const std::string& detail = "");
};
