#include "HalSharedSpi.h"

#if BISCUIT_BOARD_M5PAPER
#include <cassert>

namespace {
SemaphoreHandle_t sharedSpiMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  assert(mutex != nullptr);
  return mutex;
}
}  // namespace

HalSharedSpiLock::HalSharedSpiLock() : mutex(sharedSpiMutex()) {
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

HalSharedSpiLock::~HalSharedSpiLock() {
  xSemaphoreGiveRecursive(mutex);
}
#endif
