#include <HalDisplay.h>
#include <HalGPIO.h>  // EPD pin defines

// Global HalDisplay instance
HalDisplay display;

#if BISCUIT_BOARD_M5PAPER

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

namespace {
void* allocateDisplayMemory(size_t bytes) {
  void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ptr) {
    ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  return ptr;
}

uint8_t readBitmapByte(const uint8_t* data, size_t index, bool fromProgmem) {
  return fromProgmem ? pgm_read_byte(data + index) : data[index];
}

bool bitmapPixelIsWhite(const uint8_t* imageData, uint16_t width, uint16_t x, uint16_t y, bool fromProgmem) {
  const uint16_t widthBytes = (width + 7) / 8;
  const size_t byteIndex = static_cast<size_t>(y) * widthBytes + (x / 8);
  const uint8_t bitMask = 0x80 >> (x % 8);
  return (readBitmapByte(imageData, byteIndex, fromProgmem) & bitMask) != 0;
}

void setPackedPixel(uint8_t* buffer, uint16_t x, uint16_t y, bool white) {
  if (!buffer || x >= HalDisplay::DISPLAY_WIDTH || y >= HalDisplay::DISPLAY_HEIGHT) {
    return;
  }
  const size_t byteIndex = static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES + (x / 8);
  const uint8_t bitMask = 0x80 >> (x % 8);
  if (white) {
    buffer[byteIndex] |= bitMask;
  } else {
    buffer[byteIndex] &= ~bitMask;
  }
}

m5gfx::epd_mode_t convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return m5gfx::epd_quality;
    case HalDisplay::HALF_REFRESH:
      return m5gfx::epd_text;
    case HalDisplay::FAST_REFRESH:
    default:
      return m5gfx::epd_fastest;
  }
}
}  // namespace

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  if (frameBuffer) {
    heap_caps_free(frameBuffer);
  }
  if (grayscaleLsbBuffer) {
    heap_caps_free(grayscaleLsbBuffer);
  }
  if (grayscaleMsbBuffer) {
    heap_caps_free(grayscaleMsbBuffer);
  }
  if (nativeFrameBuffer) {
    heap_caps_free(nativeFrameBuffer);
  }
}

void HalDisplay::begin() {
  if (!frameBuffer) {
    frameBuffer = static_cast<uint8_t*>(allocateDisplayMemory(BUFFER_SIZE));
  }
  if (!nativeFrameBuffer) {
    nativeFrameBuffer = static_cast<uint16_t*>(allocateDisplayMemory(static_cast<size_t>(DISPLAY_WIDTH) *
                                                                     DISPLAY_HEIGHT * sizeof(uint16_t)));
  }
  clearScreen();
  M5.Display.setRotation(1);
  M5.Display.setColorDepth(16);
  M5.Display.setEpdMode(m5gfx::epd_quality);
  M5.Display.clear(TFT_WHITE);
}

void HalDisplay::clearScreen(uint8_t color) const {
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  if (!frameBuffer || !imageData) {
    return;
  }
  const uint16_t maxX = std::min<uint16_t>(DISPLAY_WIDTH, x + w);
  const uint16_t maxY = std::min<uint16_t>(DISPLAY_HEIGHT, y + h);
  for (uint16_t py = y; py < maxY; ++py) {
    for (uint16_t px = x; px < maxX; ++px) {
      const bool white = bitmapPixelIsWhite(imageData, w, px - x, py - y, fromProgmem);
      setPackedPixel(frameBuffer, px, py, white);
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  if (!frameBuffer || !imageData) {
    return;
  }
  const uint16_t maxX = std::min<uint16_t>(DISPLAY_WIDTH, x + w);
  const uint16_t maxY = std::min<uint16_t>(DISPLAY_HEIGHT, y + h);
  for (uint16_t py = y; py < maxY; ++py) {
    for (uint16_t px = x; px < maxX; ++px) {
      const bool white = bitmapPixelIsWhite(imageData, w, px - x, py - y, fromProgmem);
      if (!white) {
        setPackedPixel(frameBuffer, px, py, false);
      }
    }
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (!frameBuffer || !nativeFrameBuffer) {
    return;
  }
  const uint16_t black = M5.Display.color565(0, 0, 0);
  const uint16_t white = M5.Display.color565(255, 255, 255);
  for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
    for (uint16_t x = 0; x < DISPLAY_WIDTH; ++x) {
      const size_t byteIndex = static_cast<size_t>(y) * DISPLAY_WIDTH_BYTES + (x / 8);
      const uint8_t bitMask = 0x80 >> (x % 8);
      nativeFrameBuffer[static_cast<size_t>(y) * DISPLAY_WIDTH + x] = (frameBuffer[byteIndex] & bitMask) ? white : black;
    }
  }
  M5.Display.setEpdMode(convertRefreshMode(mode));
  M5.Display.pushImage(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, nativeFrameBuffer);
  if (turnOffScreen) {
    M5.Display.sleep();
  }
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::deepSleep() { M5.Display.sleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) {
    return;
  }
  if (!grayscaleLsbBuffer) {
    grayscaleLsbBuffer = static_cast<uint8_t*>(allocateDisplayMemory(BUFFER_SIZE));
  }
  if (grayscaleLsbBuffer) {
    memcpy(grayscaleLsbBuffer, lsbBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) {
    return;
  }
  if (!grayscaleMsbBuffer) {
    grayscaleMsbBuffer = static_cast<uint8_t*>(allocateDisplayMemory(BUFFER_SIZE));
  }
  if (grayscaleMsbBuffer) {
    memcpy(grayscaleMsbBuffer, msbBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  if (bwBuffer && frameBuffer && bwBuffer != frameBuffer) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { displayBuffer(HALF_REFRESH, turnOffScreen); }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }

uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }

uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }

uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

#else

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin() {
  einkDisplay.begin();
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }

uint16_t HalDisplay::getDisplayWidth() const { return EInkDisplay::DISPLAY_WIDTH; }

uint16_t HalDisplay::getDisplayHeight() const { return EInkDisplay::DISPLAY_HEIGHT; }

uint16_t HalDisplay::getDisplayWidthBytes() const { return EInkDisplay::DISPLAY_WIDTH_BYTES; }

uint32_t HalDisplay::getBufferSize() const { return EInkDisplay::BUFFER_SIZE; }

#endif
