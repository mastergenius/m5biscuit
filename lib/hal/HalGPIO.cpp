#include <HalGPIO.h>
#include <Logging.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

#if BISCUIT_BOARD_M5PAPER

#include <M5Unified.h>

namespace {
constexpr int M5PAPER_TOUCH_WIDTH = BISCUIT_DISPLAY_HEIGHT;    // portrait touch coordinates: 540 px
constexpr int M5PAPER_TOUCH_HEIGHT = BISCUIT_DISPLAY_WIDTH;    // portrait touch coordinates: 960 px
constexpr int M5PAPER_TOUCH_BOTTOM_HIT_HEIGHT = 80;
constexpr int M5PAPER_TOUCH_SIDE_HIT_WIDTH = 72;
constexpr int M5PAPER_TOUCH_SDA = 21;
constexpr int M5PAPER_TOUCH_SCL = 22;
constexpr uint32_t M5PAPER_TOUCH_I2C_FREQ = 400000;
constexpr uint8_t GT911_ADDR_5D = 0x5D;
constexpr uint8_t GT911_ADDR_14 = 0x14;
constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;
constexpr uint16_t GT911_STATUS_REG = 0x814E;
constexpr uint16_t GT911_FIRST_POINT_REG = 0x814F;

bool readActiveLowButton(uint8_t pin) { return digitalRead(pin) == LOW; }

enum class M5PaperTouchSource : uint8_t { None, M5Unified, M5GfxRaw, Gt911Wire };

uint8_t gt911Address = 0;
bool m5paperTouchWasPressed = false;

const char* touchSourceName(M5PaperTouchSource source) {
  switch (source) {
    case M5PaperTouchSource::M5Unified:
      return "M5Unified";
    case M5PaperTouchSource::M5GfxRaw:
      return "M5GFXRaw";
    case M5PaperTouchSource::Gt911Wire:
      return "GT911Wire";
    case M5PaperTouchSource::None:
    default:
      return "None";
  }
}

void normalizeM5PaperDisplayTouch(int* x, int* y) {
  if (M5.Display.width() == M5PAPER_TOUCH_HEIGHT && M5.Display.height() == M5PAPER_TOUCH_WIDTH) {
    const int rawX = *x;
    const int rawY = *y;
    if (rawX >= 0 && rawX < M5PAPER_TOUCH_HEIGHT && rawY >= 0 && rawY < M5PAPER_TOUCH_WIDTH) {
      *x = M5PAPER_TOUCH_WIDTH - 1 - rawY;
      *y = rawX;
    }
  }
}

bool gt911Read(uint8_t address, uint16_t reg, uint8_t* data, size_t len) {
  Wire1.beginTransmission(address);
  Wire1.write(static_cast<uint8_t>(reg >> 8));
  Wire1.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire1.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire1.requestFrom(static_cast<int>(address), static_cast<int>(len));
  for (size_t i = 0; i < received && i < len; ++i) {
    data[i] = Wire1.read();
  }
  while (Wire1.available()) {
    Wire1.read();
  }
  return received == len;
}

bool gt911WriteByte(uint8_t address, uint16_t reg, uint8_t value) {
  Wire1.beginTransmission(address);
  Wire1.write(static_cast<uint8_t>(reg >> 8));
  Wire1.write(static_cast<uint8_t>(reg & 0xFF));
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

uint8_t probeGt911Address() {
  uint8_t productId[4] = {};
  for (const uint8_t address : {GT911_ADDR_5D, GT911_ADDR_14}) {
    if (gt911Read(address, GT911_PRODUCT_ID_REG, productId, sizeof(productId))) {
      gt911WriteByte(address, GT911_STATUS_REG, 0);
      return address;
    }
  }
  return 0;
}

bool readGt911WireTouch(int* outX, int* outY) {
  if (gt911Address == 0) {
    return false;
  }

  uint8_t status = 0;
  if (!gt911Read(gt911Address, GT911_STATUS_REG, &status, 1) || (status & 0x80) == 0) {
    return false;
  }

  const uint8_t pointCount = status & 0x0F;
  if (pointCount == 0 || pointCount > 5) {
    gt911WriteByte(gt911Address, GT911_STATUS_REG, 0);
    return false;
  }

  uint8_t point[8] = {};
  const bool ok = gt911Read(gt911Address, GT911_FIRST_POINT_REG, point, sizeof(point));
  gt911WriteByte(gt911Address, GT911_STATUS_REG, 0);
  if (!ok) {
    return false;
  }

  *outX = point[1] | (static_cast<int>(point[2]) << 8);
  *outY = point[3] | (static_cast<int>(point[4]) << 8);
  return true;
}

void applyM5PaperTouchPoint(bool nextState[7], int x, int y) {
  if (x < 0 || y < 0 || x >= M5PAPER_TOUCH_WIDTH || y >= M5PAPER_TOUCH_HEIGHT) {
    return;
  }

  if (y >= M5PAPER_TOUCH_HEIGHT - M5PAPER_TOUCH_BOTTOM_HIT_HEIGHT) {
    const int zone = (x * 4) / M5PAPER_TOUCH_WIDTH;
    switch (zone) {
      case 0:
        nextState[HalGPIO::BTN_BACK] = true;
        break;
      case 1:
        nextState[HalGPIO::BTN_CONFIRM] = true;
        break;
      case 2:
        nextState[HalGPIO::BTN_LEFT] = true;
        break;
      default:
        nextState[HalGPIO::BTN_RIGHT] = true;
        break;
    }
    return;
  }

  if (x >= M5PAPER_TOUCH_WIDTH - M5PAPER_TOUCH_SIDE_HIT_WIDTH) {
    const int sideHeight = M5PAPER_TOUCH_HEIGHT - M5PAPER_TOUCH_BOTTOM_HIT_HEIGHT;
    nextState[y < sideHeight / 2 ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN] = true;
  }
}

void applyM5PaperTouchZones(bool nextState[7]) {
  bool touched = false;
  int logX = -1;
  int logY = -1;
  int logRawX = -1;
  int logRawY = -1;
  M5PaperTouchSource logSource = M5PaperTouchSource::None;

  if (M5.Touch.isEnabled()) {
    const uint8_t touchCount = M5.Touch.getCount();
    for (uint8_t i = 0; i < touchCount; ++i) {
      const auto& touch = M5.Touch.getDetail(i);
      if (!touch.isPressed()) {
        continue;
      }

      int x = touch.x;
      int y = touch.y;
      normalizeM5PaperDisplayTouch(&x, &y);
      touched = true;
      if (logSource == M5PaperTouchSource::None) {
        logRawX = touch.x;
        logRawY = touch.y;
        logX = x;
        logY = y;
        logSource = M5PaperTouchSource::M5Unified;
      }
      applyM5PaperTouchPoint(nextState, x, y);
    }
  }

  if (!touched) {
    m5gfx::touch_point_t point = {};
    if (M5.Display.getTouch(&point, 1) > 0) {
      int x = point.x;
      int y = point.y;
      normalizeM5PaperDisplayTouch(&x, &y);
      touched = true;
      logRawX = point.x;
      logRawY = point.y;
      logX = x;
      logY = y;
      logSource = M5PaperTouchSource::M5GfxRaw;
      applyM5PaperTouchPoint(nextState, x, y);
    }
  }

  if (!touched) {
    int x = 0;
    int y = 0;
    if (readGt911WireTouch(&x, &y)) {
      touched = true;
      logRawX = x;
      logRawY = y;
      logX = x;
      logY = y;
      logSource = M5PaperTouchSource::Gt911Wire;
      applyM5PaperTouchPoint(nextState, x, y);
    }
  }

  if (touched && !m5paperTouchWasPressed) {
    LOG_INF("GPIO", "M5Paper touch down source=%s raw=%d,%d mapped=%d,%d", touchSourceName(logSource), logRawX,
            logRawY, logX, logY);
  } else if (!touched && m5paperTouchWasPressed) {
    LOG_INF("GPIO", "M5Paper touch up");
  }
  m5paperTouchWasPressed = touched;
}
}  // namespace

void HalGPIO::begin() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  cfg.clear_display = false;
  cfg.output_power = true;
  M5.begin(cfg);

  const bool wireStarted = Wire1.begin(M5PAPER_TOUCH_SDA, M5PAPER_TOUCH_SCL, M5PAPER_TOUCH_I2C_FREQ);
  Wire1.setTimeOut(5);
  gt911Address = probeGt911Address();
  LOG_INF("GPIO", "M5Paper touch init: m5=%d display=%d wire1=%d gt911=0x%02x", M5.Touch.isEnabled(),
          M5.Display.touch() != nullptr, wireStarted, gt911Address);

  _deviceType = DeviceType::M5Paper;
  pinMode(M5PAPER_BTN_LEFT, INPUT);
  pinMode(M5PAPER_BTN_PUSH, INPUT);
  pinMode(M5PAPER_BTN_RIGHT, INPUT);
  SPI.begin(M5PAPER_SPI_SCLK, M5PAPER_SPI_MISO, M5PAPER_SPI_MOSI, M5PAPER_EPD_CS);
  update();
  lastUsbConnected = isUsbConnected();
  usbStateChanged = false;
}

void HalGPIO::update() {
  M5.update();

  bool nextState[7] = {};
  const bool left = readActiveLowButton(M5PAPER_BTN_LEFT);
  const bool push = readActiveLowButton(M5PAPER_BTN_PUSH);
  const bool right = readActiveLowButton(M5PAPER_BTN_RIGHT);

  nextState[BTN_BACK] = left;
  nextState[BTN_LEFT] = left;
  nextState[BTN_CONFIRM] = push;
  nextState[BTN_POWER] = push;
  nextState[BTN_RIGHT] = right;
  applyM5PaperTouchZones(nextState);

  bool anyPressed = false;
  for (uint8_t i = 0; i < 7; ++i) {
    pressedEdge[i] = nextState[i] && !buttonState[i];
    releasedEdge[i] = !nextState[i] && buttonState[i];
    buttonState[i] = nextState[i];
    anyPressed = anyPressed || nextState[i];
  }

  if (wasAnyPressed()) {
    heldStartMs = millis();
  }
  heldTimeMs = anyPressed ? (millis() - heldStartMs) : 0;

  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return buttonIndex < 7 && buttonState[buttonIndex]; }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return buttonIndex < 7 && pressedEdge[buttonIndex]; }

bool HalGPIO::wasAnyPressed() const {
  for (bool edge : pressedEdge) {
    if (edge) {
      return true;
    }
  }
  return false;
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return buttonIndex < 7 && releasedEdge[buttonIndex]; }

bool HalGPIO::wasAnyReleased() const {
  for (bool edge : releasedEdge) {
    if (edge) {
      return true;
    }
  }
  return false;
}

unsigned long HalGPIO::getHeldTime() const { return heldTimeMs; }

void HalGPIO::startDeepSleep() {
  while (isPressed(BTN_POWER)) {
    delay(50);
    update();
  }
  M5.Power.deepSleep();
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  (void)requiredDurationMs;
  (void)shortPressAllowed;
}

bool HalGPIO::isUsbConnected() const { return M5.Power.isCharging() == m5::Power_Class::is_charging; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && isUsbConnected()) {
    return WakeupReason::AfterFlash;
  }
  return WakeupReason::Other;
}

#else

namespace X3GPIO {

struct X3ProbeResult {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;

  uint8_t score() const {
    return static_cast<uint8_t>(bq27220) + static_cast<uint8_t>(ds3231) + static_cast<uint8_t>(qmi8658);
  }
};

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  *outValue = Wire.read();
  return true;
}

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

bool probeBQ27220Signature() {
  uint16_t soc = 0;
  uint16_t voltageMv = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) {
    return false;
  }
  if (soc > 100) {
    return false;
  }
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) {
    return false;
  }
  return voltageMv >= 2500 && voltageMv <= 5000;
}

bool probeDS3231Signature() {
  uint8_t sec = 0;
  if (!readI2CReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) {
    return false;
  }
  const uint8_t tensDigit = (sec >> 4) & 0x07;
  const uint8_t onesDigit = sec & 0x0F;

  return tensDigit <= 5 && onesDigit <= 9;
}

bool probeQMI8658Signature() {
  uint8_t whoami = 0;
  if (readI2CReg8(I2C_ADDR_QMI8658, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  if (readI2CReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  return false;
}

X3ProbeResult runX3ProbePass() {
  X3ProbeResult result;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);

  result.bq27220 = probeBQ27220Signature();
  result.ds3231 = probeDS3231Signature();
  result.qmi8658 = probeQMI8658Signature();

  Wire.end();
  pinMode(20, INPUT);
  pinMode(0, INPUT);
  return result;
}

}  // namespace X3GPIO

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) {
    return defaultValue;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(defaultValue));
  prefs.end();
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  // 0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Device override active: %s", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Using cached device type: %s", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: run active X3 fingerprint probe and persist result.
  const X3GPIO::X3ProbeResult pass1 = X3GPIO::runX3ProbePass();
  delay(2);
  const X3GPIO::X3ProbeResult pass2 = X3GPIO::runX3ProbePass();

  const uint8_t score1 = pass1.score();
  const uint8_t score2 = pass2.score();
  LOG_INF("HW", "X3 probe scores: pass1=%u(bq=%d rtc=%d imu=%d) pass2=%u(bq=%d rtc=%d imu=%d)", score1, pass1.bq27220,
          pass1.ds3231, pass1.qmi8658, score2, pass2.bq27220, pass2.ds3231, pass2.qmi8658);
  const bool x3Confirmed = (score1 >= 2) && (score2 >= 2);
  const bool x4Confirmed = (score1 == 0) && (score2 == 0);

  if (x3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }

  if (x4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  return HalGPIO::DeviceType::X4;
}

}  // namespace

void HalGPIO::begin() {
  inputMgr.begin();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  _deviceType = detectDeviceTypeWithFingerprint();

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
}

void HalGPIO::update() {
  inputMgr.update();
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::startDeepSleep() {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (inputMgr.isPressed(BTN_POWER)) {
    delay(50);
    inputMgr.update();
  }
  // Arm the wakeup trigger *after* the button is released
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  if (shortPressAllowed) {
    // Fast path - no duration check needed
    return;
  }
  // TODO: Intermittent edge case remains: a single tap followed by another single tap
  // can still power on the device. Tighten wake debounce/state handling here.

  // Calibrate: subtract boot time already elapsed, assuming button held since boot
  const uint16_t calibration = millis();
  const uint16_t calibratedDuration = (calibration < requiredDurationMs) ? (requiredDurationMs - calibration) : 1;

  const auto start = millis();
  inputMgr.update();
  // inputMgr.isPressed() may take up to ~500ms to return correct state
  while (!inputMgr.isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    inputMgr.update();
  }
  if (inputMgr.isPressed(BTN_POWER)) {
    do {
      delay(10);
      inputMgr.update();
    } while (inputMgr.isPressed(BTN_POWER) && inputMgr.getHeldTime() < calibratedDuration);
    if (inputMgr.getHeldTime() < calibratedDuration) {
      startDeepSleep();
    }
  } else {
    startDeepSleep();
  }
}

bool HalGPIO::isUsbConnected() const {
  if (deviceIsX3()) {
    // X3: infer USB/charging via BQ27220 Current() register (0x0C, signed mA).
    // Positive current means charging.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        return currentMa > 0;
      }
      delay(2);
    }
    return false;
  }
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}

#endif
