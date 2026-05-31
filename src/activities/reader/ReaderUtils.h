#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = !SETTINGS.longPressChapterSkip;
  const bool prev = usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) ||
                                input.wasPressed(MappedInputManager::Button::Left))
                             : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                input.wasReleased(MappedInputManager::Button::Left));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasPressed(MappedInputManager::Button::Right))
                             : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasReleased(MappedInputManager::Button::Right));
  return {prev, next};
}

inline bool consumeFullRefreshCycle(int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return true;
  }
  pagesUntilFullRefresh--;
  return false;
}

inline HalDisplay::RefreshMode nextRefreshMode(int& pagesUntilFullRefresh) {
  return consumeFullRefreshCycle(pagesUntilFullRefresh) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
}

inline HalDisplay::RefreshMode nextNativeGrayscaleRefreshMode(int& pagesUntilFullRefresh) {
  return consumeFullRefreshCycle(pagesUntilFullRefresh) ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH;
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  renderer.displayBuffer(nextRefreshMode(pagesUntilFullRefresh));
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
bool renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn,
                       const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) {
  if (!renderer.supportsGrayscale()) {
    return false;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return false;
  }

  if (renderer.usesNativeGrayscaleFramebuffer()) {
    renderer.copyGrayscaleBwBuffer();
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(refreshMode);
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
  return true;
}

}  // namespace ReaderUtils
