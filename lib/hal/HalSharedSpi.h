#pragma once

#include "HalBoard.h"

#if BISCUIT_BOARD_M5PAPER
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class HalSharedSpiLock {
 public:
  HalSharedSpiLock();
  ~HalSharedSpiLock();

  HalSharedSpiLock(const HalSharedSpiLock&) = delete;
  HalSharedSpiLock& operator=(const HalSharedSpiLock&) = delete;

 private:
  SemaphoreHandle_t mutex = nullptr;
};
#else
class HalSharedSpiLock {
 public:
  HalSharedSpiLock() = default;
  ~HalSharedSpiLock() = default;
};
#endif
