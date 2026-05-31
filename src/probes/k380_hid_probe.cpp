#include <Arduino.h>
#include <esp32-hal-bt-mem.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "esp_hid_common.h"
#include "esp_hidh.h"
#include "esp_hidh_bluedroid.h"
#include "esp_hidh_gattc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
}

namespace {

constexpr const char *kTag = "K380Probe";
constexpr uint32_t kStatusIntervalMs = 5000;
constexpr uint32_t kScanRestartIntervalMs = 15000;
constexpr uint32_t kRetryAfterFailureMs = 8000;
constexpr uint8_t kInquiryLengthUnits = 10;  // 10 * 1.28s, the Classic BT inquiry unit.
constexpr uint32_t kBleScanDurationSeconds = 13;
constexpr uint32_t kCodMajorMask = 0x001f00;
constexpr uint32_t kCodMajorPeripheral = 0x000500;
constexpr uint32_t kCodKeyboardBit = 0x000040;

bool s_btReady = false;
bool s_hidReady = false;
bool s_classicScanActive = false;
bool s_bleScanActive = false;
bool s_bleScanRequested = false;
bool s_bleScanStartPending = false;
bool s_bleScanParamsReady = false;
bool s_connecting = false;
bool s_connected = false;
bool s_pairing = false;
bool s_connectQueued = false;
uint32_t s_lastStatusMs = 0;
uint32_t s_lastScanFinishedMs = 0;
uint32_t s_nextScanAllowedMs = 0;
uint32_t s_bleSuppressedLogCount = 0;
esp_bd_addr_t s_candidateAddr = {0};
esp_hid_transport_t s_candidateTransport = ESP_HID_TRANSPORT_MAX;
uint8_t s_candidateBleAddrType = 0;
char s_candidateName[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
char s_initFailure[96] = {0};
esp_hidh_dev_t *s_device = nullptr;

esp_ble_scan_params_t s_bleScanParams = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
};

void setInitFailure(const char *step, esp_err_t err) {
  std::snprintf(s_initFailure, sizeof(s_initFailure), "%s: %s", step, esp_err_to_name(err));
}

void printAddress(const uint8_t *addr, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  if (addr == nullptr || outLen < 18) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, outLen, "%02x:%02x:%02x:%02x:%02x:%02x",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void copyAddress(esp_bd_addr_t dst, const esp_bd_addr_t src) {
  std::memcpy(dst, src, ESP_BD_ADDR_LEN);
}

bool containsCaseInsensitive(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr || *needle == '\0') {
    return false;
  }

  const size_t needleLen = std::strlen(needle);
  for (const char *p = haystack; *p != '\0'; ++p) {
    size_t i = 0;
    while (i < needleLen && p[i] != '\0' &&
           std::tolower(static_cast<unsigned char>(p[i])) ==
               std::tolower(static_cast<unsigned char>(needle[i]))) {
      ++i;
    }
    if (i == needleLen) {
      return true;
    }
  }
  return false;
}

bool isClassicKeyboardCod(uint32_t cod) {
  return (cod & kCodMajorMask) == kCodMajorPeripheral && (cod & kCodKeyboardBit) != 0;
}

const char *transportName(esp_hid_transport_t transport) {
  switch (transport) {
    case ESP_HID_TRANSPORT_BT:
      return "Classic";
    case ESP_HID_TRANSPORT_BLE:
      return "BLE";
    case ESP_HID_TRANSPORT_USB:
      return "USB";
    default:
      return "unknown";
  }
}

bool scanBusy() {
  return s_classicScanActive || s_bleScanActive || s_bleScanRequested || s_bleScanStartPending;
}

void copyEirName(uint8_t *eir, char *name, size_t nameLen) {
  if (eir == nullptr || name == nullptr || nameLen == 0) {
    return;
  }

  uint8_t len = 0;
  uint8_t *eirName = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
  if (eirName == nullptr || len == 0) {
    eirName = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
  }
  if (eirName == nullptr || len == 0) {
    return;
  }

  const size_t copyLen = std::min(static_cast<size_t>(len), nameLen - 1);
  std::memcpy(name, eirName, copyLen);
  name[copyLen] = '\0';
}

void copyBleAdvString(const uint8_t *advData,
                      uint16_t advLen,
                      esp_ble_adv_data_type type,
                      char *out,
                      size_t outLen) {
  if (advData == nullptr || out == nullptr || outLen == 0) {
    return;
  }

  uint8_t len = 0;
  uint8_t *field = esp_ble_resolve_adv_data_by_type(const_cast<uint8_t *>(advData), advLen, type, &len);
  if (field == nullptr || len == 0) {
    return;
  }

  const size_t copyLen = std::min(static_cast<size_t>(len), outLen - 1);
  std::memcpy(out, field, copyLen);
  out[copyLen] = '\0';
}

uint16_t readBleAdvU16(const uint8_t *advData, uint16_t advLen, esp_ble_adv_data_type type) {
  if (advData == nullptr) {
    return 0;
  }

  uint8_t len = 0;
  uint8_t *field = esp_ble_resolve_adv_data_by_type(const_cast<uint8_t *>(advData), advLen, type, &len);
  if (field == nullptr || len < 2) {
    return 0;
  }
  return static_cast<uint16_t>(field[0]) | (static_cast<uint16_t>(field[1]) << 8);
}

bool bleAdvHasUuid16(const uint8_t *advData, uint16_t advLen, uint16_t uuid) {
  if (advData == nullptr) {
    return false;
  }

  const esp_ble_adv_data_type uuidTypes[] = {
      ESP_BLE_AD_TYPE_16SRV_PART,
      ESP_BLE_AD_TYPE_16SRV_CMPL,
      ESP_BLE_AD_TYPE_SOL_SRV_UUID,
  };
  for (esp_ble_adv_data_type type : uuidTypes) {
    uint8_t len = 0;
    uint8_t *field = esp_ble_resolve_adv_data_by_type(const_cast<uint8_t *>(advData), advLen, type, &len);
    for (uint8_t i = 0; field != nullptr && i + 1 < len; i += 2) {
      const uint16_t found = static_cast<uint16_t>(field[i]) | (static_cast<uint16_t>(field[i + 1]) << 8);
      if (found == uuid) {
        return true;
      }
    }
  }
  return false;
}

const char *gapEventName(esp_bt_gap_cb_event_t event) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: return "DISC_RES";
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: return "DISC_STATE";
    case ESP_BT_GAP_AUTH_CMPL_EVT: return "AUTH_CMPL";
    case ESP_BT_GAP_PIN_REQ_EVT: return "PIN_REQ";
    case ESP_BT_GAP_CFM_REQ_EVT: return "CFM_REQ";
    case ESP_BT_GAP_KEY_NOTIF_EVT: return "KEY_NOTIF";
    case ESP_BT_GAP_KEY_REQ_EVT: return "KEY_REQ";
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT: return "ACL_CONN";
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT: return "ACL_DISCONN";
    case ESP_BT_GAP_MODE_CHG_EVT: return "MODE_CHG";
    default: return "OTHER";
  }
}

const char *bleGapEventName(esp_gap_ble_cb_event_t event) {
  switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: return "SCAN_PARAM_SET";
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: return "SCAN_START";
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: return "SCAN_STOP";
    case ESP_GAP_BLE_SCAN_RESULT_EVT: return "SCAN_RESULT";
    case ESP_GAP_BLE_AUTH_CMPL_EVT: return "AUTH_CMPL";
    case ESP_GAP_BLE_KEY_EVT: return "KEY";
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: return "PASSKEY_NOTIF";
    case ESP_GAP_BLE_NC_REQ_EVT: return "NC_REQ";
    case ESP_GAP_BLE_PASSKEY_REQ_EVT: return "PASSKEY_REQ";
    case ESP_GAP_BLE_SEC_REQ_EVT: return "SEC_REQ";
    default: return "OTHER";
  }
}

const char *hidEventName(esp_hidh_event_t event) {
  switch (event) {
    case ESP_HIDH_OPEN_EVENT: return "OPEN";
    case ESP_HIDH_BATTERY_EVENT: return "BATTERY";
    case ESP_HIDH_INPUT_EVENT: return "INPUT";
    case ESP_HIDH_FEATURE_EVENT: return "FEATURE";
    case ESP_HIDH_CLOSE_EVENT: return "CLOSE";
    case ESP_HIDH_START_EVENT: return "START";
    case ESP_HIDH_STOP_EVENT: return "STOP";
    default: return "OTHER";
  }
}

bool looksLikeK380(const char *name) {
  return containsCaseInsensitive(name, "K380") ||
         containsCaseInsensitive(name, "Keyboard K380") ||
         containsCaseInsensitive(name, "Logi K380") ||
         containsCaseInsensitive(name, "Logitech K380");
}

void printBonds() {
  if (!s_btReady) {
    Serial.println("[K380] bonds: Bluetooth is not ready");
    return;
  }

  const int total = esp_bt_gap_get_bond_device_num();
  Serial.printf("[K380] bonded Classic BT devices: %d\n", total);
  if (total > 0) {
    esp_bd_addr_t *devices = static_cast<esp_bd_addr_t *>(std::calloc(total, sizeof(esp_bd_addr_t)));
    if (devices == nullptr) {
      Serial.println("[K380] bonds: allocation failed");
    } else {
      int count = total;
      const esp_err_t err = esp_bt_gap_get_bond_device_list(&count, devices);
      if (err != ESP_OK) {
        Serial.printf("[K380] bonds: list failed: %s\n", esp_err_to_name(err));
      } else {
        for (int i = 0; i < count; ++i) {
          char addrText[18];
          printAddress(devices[i], addrText, sizeof(addrText));
          Serial.printf("[K380] bond[%d]=%s\n", i, addrText);
        }
      }
      std::free(devices);
    }
  }

  const int bleTotal = esp_ble_get_bond_device_num();
  Serial.printf("[K380] bonded BLE devices: %d\n", bleTotal);
  if (bleTotal <= 0) {
    return;
  }

  esp_ble_bond_dev_t *bleDevices =
      static_cast<esp_ble_bond_dev_t *>(std::calloc(bleTotal, sizeof(esp_ble_bond_dev_t)));
  if (bleDevices == nullptr) {
    Serial.println("[K380] BLE bonds: allocation failed");
    return;
  }

  int bleCount = bleTotal;
  const esp_err_t bleErr = esp_ble_get_bond_device_list(&bleCount, bleDevices);
  if (bleErr != ESP_OK) {
    Serial.printf("[K380] BLE bonds: list failed: %s\n", esp_err_to_name(bleErr));
    std::free(bleDevices);
    return;
  }

  for (int i = 0; i < bleCount; ++i) {
    char addrText[18];
    printAddress(bleDevices[i].bd_addr, addrText, sizeof(addrText));
    Serial.printf("[K380] ble_bond[%d]=%s type=%u\n",
                  i,
                  addrText,
                  static_cast<unsigned>(bleDevices[i].bd_addr_type));
  }
  std::free(bleDevices);
}

void clearBonds() {
  if (!s_btReady) {
    Serial.println("[K380] clear-bonds: Bluetooth is not ready");
    return;
  }

  const int total = esp_bt_gap_get_bond_device_num();
  Serial.printf("[K380] clearing bonded Classic BT devices: %d\n", total);
  if (total > 0) {
    esp_bd_addr_t *devices = static_cast<esp_bd_addr_t *>(std::calloc(total, sizeof(esp_bd_addr_t)));
    if (devices == nullptr) {
      Serial.println("[K380] clear-bonds: allocation failed");
    } else {
      int count = total;
      const esp_err_t err = esp_bt_gap_get_bond_device_list(&count, devices);
      if (err != ESP_OK) {
        Serial.printf("[K380] clear-bonds: list failed: %s\n", esp_err_to_name(err));
      } else {
        for (int i = 0; i < count; ++i) {
          char addrText[18];
          printAddress(devices[i], addrText, sizeof(addrText));
          const esp_err_t removeErr = esp_bt_gap_remove_bond_device(devices[i]);
          Serial.printf("[K380] clear-bonds: %s -> %s\n", addrText, esp_err_to_name(removeErr));
        }
      }
      std::free(devices);
    }
  }

  const int bleTotal = esp_ble_get_bond_device_num();
  Serial.printf("[K380] clearing bonded BLE devices: %d\n", bleTotal);
  if (bleTotal <= 0) {
    return;
  }

  esp_ble_bond_dev_t *bleDevices =
      static_cast<esp_ble_bond_dev_t *>(std::calloc(bleTotal, sizeof(esp_ble_bond_dev_t)));
  if (bleDevices == nullptr) {
    Serial.println("[K380] clear BLE bonds: allocation failed");
    return;
  }

  int bleCount = bleTotal;
  const esp_err_t bleErr = esp_ble_get_bond_device_list(&bleCount, bleDevices);
  if (bleErr != ESP_OK) {
    Serial.printf("[K380] clear BLE bonds: list failed: %s\n", esp_err_to_name(bleErr));
    std::free(bleDevices);
    return;
  }

  for (int i = 0; i < bleCount; ++i) {
    char addrText[18];
    printAddress(bleDevices[i].bd_addr, addrText, sizeof(addrText));
    const esp_err_t removeErr = esp_ble_remove_bond_device(bleDevices[i].bd_addr);
    Serial.printf("[K380] clear BLE bond: %s -> %s\n", addrText, esp_err_to_name(removeErr));
  }
  std::free(bleDevices);
}

void printStatus() {
  Serial.printf("[K380] status bt=%d hid=%d classic_scan=%d ble_scan=%d ble_pending=%d connecting=%d pairing=%d connected=%d heap=%u psram=%u\n",
                s_btReady,
                s_hidReady,
                s_classicScanActive,
                s_bleScanActive,
                s_bleScanRequested || s_bleScanStartPending,
                s_connecting,
                s_pairing,
                s_connected,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));
  if (s_device != nullptr) {
    const uint8_t *addr = esp_hidh_dev_bda_get(s_device);
    char addrText[18];
    printAddress(addr, addrText, sizeof(addrText));
    Serial.printf("[K380] device name='%s' addr=%s usage=%s vid=0x%04x pid=0x%04x\n",
                  esp_hidh_dev_name_get(s_device) ? esp_hidh_dev_name_get(s_device) : "",
                  addrText,
                  esp_hid_usage_str(esp_hidh_dev_usage_get(s_device)),
                  esp_hidh_dev_vendor_id_get(s_device),
                  esp_hidh_dev_product_id_get(s_device));
  }
  if (!s_btReady && s_initFailure[0] != '\0') {
    Serial.printf("[K380] last init failure: %s\n", s_initFailure);
  }
}

void openQueuedCandidate() {
  if (!s_connectQueued || s_connecting || s_connected || scanBusy()) {
    return;
  }

  char addrText[18];
  printAddress(s_candidateAddr, addrText, sizeof(addrText));
  Serial.printf("[K380] opening HID device name='%s' addr=%s transport=%s addr_type=%u\n",
                s_candidateName,
                addrText,
                transportName(s_candidateTransport),
                static_cast<unsigned>(s_candidateBleAddrType));

  s_connectQueued = false;
  s_connecting = true;
  esp_hidh_dev_t *dev = esp_hidh_dev_open(s_candidateAddr, s_candidateTransport, s_candidateBleAddrType);
  if (dev == nullptr) {
    Serial.println("[K380] esp_hidh_dev_open returned null");
    s_connecting = false;
    s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
  }
}

void cancelActiveScans() {
  if (s_classicScanActive) {
    const esp_err_t err = esp_bt_gap_cancel_discovery();
    if (err != ESP_OK) {
      Serial.printf("[K380] Classic discovery cancel failed: %s\n", esp_err_to_name(err));
      s_classicScanActive = false;
    }
  }

  if (s_bleScanActive || s_bleScanStartPending || s_bleScanRequested) {
    s_bleScanRequested = false;
    s_bleScanStartPending = false;
    const esp_err_t err = esp_ble_gap_stop_scanning();
    if (err != ESP_OK) {
      Serial.printf("[K380] BLE scan stop failed: %s\n", esp_err_to_name(err));
      s_bleScanActive = false;
    }
  }
}

void queueCandidate(const esp_bd_addr_t addr,
                    const char *name,
                    esp_hid_transport_t transport,
                    uint8_t bleAddrType = 0) {
  if (s_connectQueued || s_connecting || s_connected) {
    return;
  }

  copyAddress(s_candidateAddr, addr);
  s_candidateTransport = transport;
  s_candidateBleAddrType = bleAddrType;
  std::snprintf(s_candidateName, sizeof(s_candidateName), "%s", name ? name : "");
  s_connectQueued = true;

  if (scanBusy()) {
    Serial.printf("[K380] K380 candidate found on %s; stopping scans before opening HID\n",
                  transportName(transport));
    cancelActiveScans();
    return;
  }

  openQueuedCandidate();
}

void startBleScan() {
  if (!s_btReady || !s_hidReady || s_bleScanActive || s_bleScanRequested ||
      s_bleScanStartPending || s_connecting || s_connected || s_connectQueued) {
    return;
  }

  if (!s_bleScanParamsReady) {
    s_bleScanRequested = true;
    const esp_err_t err = esp_ble_gap_set_scan_params(&s_bleScanParams);
    if (err != ESP_OK) {
      Serial.printf("[K380] BLE scan params failed: %s\n", esp_err_to_name(err));
      s_bleScanRequested = false;
    }
    return;
  }

  s_bleScanStartPending = true;
  const esp_err_t err = esp_ble_gap_start_scanning(kBleScanDurationSeconds);
  if (err != ESP_OK) {
    Serial.printf("[K380] BLE scan start failed: %s\n", esp_err_to_name(err));
    s_bleScanStartPending = false;
  }
}

void startDiscovery(bool force = false) {
  if (!s_btReady || !s_hidReady || scanBusy() || s_connecting || s_connected || s_connectQueued) {
    return;
  }

  const uint32_t now = millis();
  if (!force && static_cast<int32_t>(now - s_nextScanAllowedMs) < 0) {
    return;
  }

  Serial.println("[K380] scanning for Logitech K380 on Classic BT + BLE; hold slot 1 until it blinks");
  bool started = false;
  s_bleSuppressedLogCount = 0;

  const esp_err_t classicErr =
      esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryLengthUnits, 0);
  if (classicErr == ESP_OK) {
    s_classicScanActive = true;
    started = true;
  } else {
    Serial.printf("[K380] Classic discovery start failed: %s\n", esp_err_to_name(classicErr));
  }

  startBleScan();
  started = started || s_bleScanActive || s_bleScanRequested || s_bleScanStartPending;

  if (!started) {
    s_nextScanAllowedMs = now + kRetryAfterFailureMs;
    return;
  }
}

void handleDiscoveryResult(esp_bt_gap_cb_param_t *param) {
  char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
  uint32_t cod = 0;
  int8_t rssi = 0;
  bool hasRssi = false;

  for (int i = 0; i < param->disc_res.num_prop; ++i) {
    const esp_bt_gap_dev_prop_t &prop = param->disc_res.prop[i];
    switch (prop.type) {
      case ESP_BT_GAP_DEV_PROP_BDNAME: {
        const size_t copyLen = std::min(static_cast<size_t>(prop.len), sizeof(name) - 1);
        std::memcpy(name, prop.val, copyLen);
        name[copyLen] = '\0';
        break;
      }
      case ESP_BT_GAP_DEV_PROP_EIR:
        if (name[0] == '\0') {
          copyEirName(static_cast<uint8_t *>(prop.val), name, sizeof(name));
        }
        break;
      case ESP_BT_GAP_DEV_PROP_COD:
        if (prop.len == sizeof(uint32_t)) {
          std::memcpy(&cod, prop.val, sizeof(cod));
        }
        break;
      case ESP_BT_GAP_DEV_PROP_RSSI:
        if (prop.len == sizeof(int8_t)) {
          std::memcpy(&rssi, prop.val, sizeof(rssi));
          hasRssi = true;
        }
        break;
      default:
        break;
    }
  }

  char addrText[18];
  printAddress(param->disc_res.bda, addrText, sizeof(addrText));
  Serial.printf("[K380] found addr=%s name='%s' cod=0x%06lx keyboard=%d",
                addrText,
                name,
                static_cast<unsigned long>(cod),
                isClassicKeyboardCod(cod));
  if (hasRssi) {
    Serial.printf(" rssi=%d", static_cast<int>(rssi));
  }
  Serial.println();

  if (looksLikeK380(name) || isClassicKeyboardCod(cod)) {
    queueCandidate(param->disc_res.bda, name, ESP_HID_TRANSPORT_BT);
  }
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
      handleDiscoveryResult(param);
      break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      s_classicScanActive = param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED;
      Serial.printf("[K380][GAP] Classic discovery %s\n", s_classicScanActive ? "started" : "stopped");
      if (!s_classicScanActive) {
        s_lastScanFinishedMs = millis();
        openQueuedCandidate();
      }
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
      char addrText[18];
      printAddress(param->auth_cmpl.bda, addrText, sizeof(addrText));
      const bool ok = param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
      Serial.printf("[K380][GAP] auth %s addr=%s name='%s'\n",
                    ok ? "ok" : "failed",
                    addrText,
                    reinterpret_cast<const char *>(param->auth_cmpl.device_name));
      s_pairing = false;
      if (!ok) {
        s_connecting = false;
        s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      }
      break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
      esp_bt_pin_code_t pinCode = {0};
      const uint32_t randomPin = esp_random() % 1000000UL;
      const int pinLen = param->pin_req.min_16_digit ? 16 : 6;
      char pinText[17] = {0};
      if (pinLen == 16) {
        std::snprintf(pinText, sizeof(pinText), "%016lu", static_cast<unsigned long>(randomPin));
      } else {
        std::snprintf(pinText, sizeof(pinText), "%06lu", static_cast<unsigned long>(randomPin));
      }
      std::memcpy(pinCode, pinText, pinLen);
      s_pairing = true;
      Serial.printf("[K380][PAIR] legacy PIN requested. Type %s on K380, then Enter.\n", pinText);
      esp_bt_gap_pin_reply(param->pin_req.bda, true, pinLen, pinCode);
      break;
    }
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      s_pairing = true;
      Serial.printf("[K380][PAIR] PASSKEY %06lu: type this on K380, then Enter.\n",
                    static_cast<unsigned long>(param->key_notif.passkey));
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      s_pairing = true;
      Serial.printf("[K380][PAIR] numeric comparison %06lu; accepting on host.\n",
                    static_cast<unsigned long>(param->cfm_req.num_val));
      esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      break;
    case ESP_BT_GAP_KEY_REQ_EVT:
      s_pairing = true;
      Serial.println("[K380][PAIR] remote asked host for passkey input; this probe has no host-side passkey entry UI");
      break;
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
      Serial.printf("[K380][GAP] ACL connect status=0x%02x\n",
                    static_cast<unsigned>(param->acl_conn_cmpl_stat.stat));
      break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
      Serial.printf("[K380][GAP] ACL disconnect reason=0x%02x\n",
                    static_cast<unsigned>(param->acl_disconn_cmpl_stat.reason));
      s_pairing = false;
      s_connecting = false;
      s_connected = false;
      s_device = nullptr;
      s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      break;
    default:
      Serial.printf("[K380][GAP] %s (%d)\n", gapEventName(event), static_cast<int>(event));
      break;
  }
}

void handleBleScanResult(esp_ble_gap_cb_param_t *param) {
  auto &scan = param->scan_rst;
  const uint16_t advLen = static_cast<uint16_t>(scan.adv_data_len + scan.scan_rsp_len);
  char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};

  copyBleAdvString(scan.ble_adv, advLen, ESP_BLE_AD_TYPE_NAME_CMPL, name, sizeof(name));
  if (name[0] == '\0') {
    copyBleAdvString(scan.ble_adv, advLen, ESP_BLE_AD_TYPE_NAME_SHORT, name, sizeof(name));
  }

  const uint16_t appearance = readBleAdvU16(scan.ble_adv, advLen, ESP_BLE_AD_TYPE_APPEARANCE);
  const bool hasHidService = bleAdvHasUuid16(scan.ble_adv, advLen, ESP_GATT_UUID_HID_SVC);
  const bool keyboardAppearance = appearance == ESP_HID_APPEARANCE_KEYBOARD;
  const bool candidate = looksLikeK380(name) || (hasHidService && keyboardAppearance);
  if (!candidate && name[0] == '\0' && !hasHidService && !keyboardAppearance) {
    ++s_bleSuppressedLogCount;
    return;
  }

  char addrText[18];
  printAddress(scan.bda, addrText, sizeof(addrText));
  Serial.printf("[K380] found BLE addr=%s type=%u name='%s' uuid_hid=%d appearance=0x%04x keyboard=%d rssi=%d\n",
                addrText,
                static_cast<unsigned>(scan.ble_addr_type),
                name,
                hasHidService,
                static_cast<unsigned>(appearance),
                keyboardAppearance,
                scan.rssi);

  if (candidate) {
    queueCandidate(scan.bda, name, ESP_HID_TRANSPORT_BLE, static_cast<uint8_t>(scan.ble_addr_type));
  }
}

void bleGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      s_bleScanParamsReady = param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
      Serial.printf("[K380][BLE] scan params %s\n", s_bleScanParamsReady ? "ready" : "failed");
      if (s_bleScanParamsReady && s_bleScanRequested) {
        s_bleScanRequested = false;
        startBleScan();
      } else if (!s_bleScanParamsReady) {
        s_bleScanRequested = false;
        s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      }
      break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
      s_bleScanStartPending = false;
      s_bleScanActive = param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
      Serial.printf("[K380][BLE] scan %s\n", s_bleScanActive ? "started" : "start failed");
      if (!s_bleScanActive) {
        s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      }
      break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      s_bleScanStartPending = false;
      s_bleScanRequested = false;
      s_bleScanActive = false;
      Serial.println("[K380][BLE] scan stopped");
      openQueuedCandidate();
      break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
      if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
        handleBleScanResult(param);
      } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
        s_bleScanActive = false;
        s_lastScanFinishedMs = millis();
        Serial.printf("[K380][BLE] scan complete responses=%d suppressed_anonymous=%lu\n",
                      param->scan_rst.num_resps,
                      static_cast<unsigned long>(s_bleSuppressedLogCount));
        openQueuedCandidate();
      }
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
      char addrText[18];
      printAddress(param->ble_security.auth_cmpl.bd_addr, addrText, sizeof(addrText));
      Serial.printf("[K380][BLE] auth %s addr=%s fail_reason=%u\n",
                    param->ble_security.auth_cmpl.success ? "ok" : "failed",
                    addrText,
                    static_cast<unsigned>(param->ble_security.auth_cmpl.fail_reason));
      s_pairing = false;
      if (!param->ble_security.auth_cmpl.success) {
        s_connecting = false;
        s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      }
      break;
    }
    case ESP_GAP_BLE_SEC_REQ_EVT:
      s_pairing = true;
      Serial.println("[K380][BLE][PAIR] security request accepted");
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
      s_pairing = true;
      Serial.printf("[K380][BLE][PAIR] PASSKEY %06lu: type this on K380, then Enter.\n",
                    static_cast<unsigned long>(param->ble_security.key_notif.passkey));
      break;
    case ESP_GAP_BLE_NC_REQ_EVT:
      s_pairing = true;
      Serial.printf("[K380][BLE][PAIR] numeric comparison %06lu; accepting on host.\n",
                    static_cast<unsigned long>(param->ble_security.key_notif.passkey));
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
      break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
      s_pairing = true;
      Serial.println("[K380][BLE][PAIR] remote asked host for passkey input; this probe has no host-side passkey entry UI");
      break;
    case ESP_GAP_BLE_KEY_EVT:
      Serial.printf("[K380][BLE] key type=%u\n",
                    static_cast<unsigned>(param->ble_security.ble_key.key_type));
      break;
    default:
      Serial.printf("[K380][BLE] %s (%d)\n", bleGapEventName(event), static_cast<int>(event));
      break;
  }
}

void hidCallback(void *, esp_event_base_t, int32_t id, void *eventData) {
  const esp_hidh_event_t event = static_cast<esp_hidh_event_t>(id);
  auto *param = static_cast<esp_hidh_event_data_t *>(eventData);
  Serial.printf("[K380][HID] %s (%ld)\n", hidEventName(event), static_cast<long>(id));

  switch (event) {
    case ESP_HIDH_START_EVENT:
      s_hidReady = param->start.status == ESP_OK;
      Serial.printf("[K380] HID host start status=%s\n", esp_err_to_name(param->start.status));
      if (s_hidReady) {
        startDiscovery(true);
      }
      break;
    case ESP_HIDH_OPEN_EVENT:
      s_connecting = false;
      if (param->open.status == ESP_OK) {
        s_device = param->open.dev;
        s_connected = true;
        s_pairing = false;
        const uint8_t *addr = esp_hidh_dev_bda_get(param->open.dev);
        char addrText[18];
        printAddress(addr, addrText, sizeof(addrText));
        Serial.printf("[K380] OPEN ok name='%s' addr=%s usage=%s vid=0x%04x pid=0x%04x\n",
                      esp_hidh_dev_name_get(param->open.dev) ? esp_hidh_dev_name_get(param->open.dev) : "",
                      addrText,
                      esp_hid_usage_str(esp_hidh_dev_usage_get(param->open.dev)),
                      esp_hidh_dev_vendor_id_get(param->open.dev),
                      esp_hidh_dev_product_id_get(param->open.dev));
        esp_hidh_dev_dump(param->open.dev, stdout);
      } else {
        Serial.printf("[K380] OPEN failed status=%s\n", esp_err_to_name(param->open.status));
        s_connected = false;
        s_pairing = false;
        s_device = nullptr;
        s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      }
      break;
    case ESP_HIDH_INPUT_EVENT: {
      const uint8_t *addr = esp_hidh_dev_bda_get(param->input.dev);
      char addrText[18];
      printAddress(addr, addrText, sizeof(addrText));
      Serial.printf("[K380] INPUT addr=%s usage=%s map=%u report=%u len=%u hex=",
                    addrText,
                    esp_hid_usage_str(param->input.usage),
                    static_cast<unsigned>(param->input.map_index),
                    static_cast<unsigned>(param->input.report_id),
                    static_cast<unsigned>(param->input.length));
      for (uint16_t i = 0; i < param->input.length; ++i) {
        Serial.printf("%02x", param->input.data[i]);
        if (i + 1 < param->input.length) {
          Serial.print(' ');
        }
      }
      Serial.println();
      break;
    }
    case ESP_HIDH_BATTERY_EVENT:
      Serial.printf("[K380] BATTERY level=%u status=%s\n",
                    static_cast<unsigned>(param->battery.level),
                    esp_err_to_name(param->battery.status));
      break;
    case ESP_HIDH_FEATURE_EVENT:
      Serial.printf("[K380] FEATURE usage=%s report=%u len=%u status=%s\n",
                    esp_hid_usage_str(param->feature.usage),
                    static_cast<unsigned>(param->feature.report_id),
                    static_cast<unsigned>(param->feature.length),
                    esp_err_to_name(param->feature.status));
      break;
    case ESP_HIDH_CLOSE_EVENT:
      Serial.printf("[K380] CLOSE status=%s reason=%d\n",
                    esp_err_to_name(param->close.status),
                    param->close.reason);
      s_pairing = false;
      s_connecting = false;
      s_connected = false;
      if (param->close.dev != nullptr) {
        esp_hidh_dev_free(param->close.dev);
      }
      s_device = nullptr;
      s_nextScanAllowedMs = millis() + kRetryAfterFailureMs;
      break;
    default:
      break;
  }
}

bool initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("[K380] NVS needs erase for this probe environment");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    Serial.printf("[K380] NVS init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool initBluetooth() {
  esp_bt_controller_status_t controllerStatus = esp_bt_controller_get_status();
  Serial.printf("[K380] controller status before init=%d\n", static_cast<int>(controllerStatus));

  if (controllerStatus == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_config_t btConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    btConfig.mode = ESP_BT_MODE_BTDM;
    btConfig.bt_max_acl_conn = 3;
    btConfig.bt_max_sync_conn = 3;
    esp_err_t err = esp_bt_controller_init(&btConfig);
    if (err != ESP_OK) {
      controllerStatus = esp_bt_controller_get_status();
      Serial.printf("[K380] controller init returned %s, status now=%d\n",
                    esp_err_to_name(err),
                    static_cast<int>(controllerStatus));
      if (err != ESP_ERR_INVALID_STATE || controllerStatus == ESP_BT_CONTROLLER_STATUS_IDLE) {
        setInitFailure("controller init", err);
        return false;
      }
    } else {
      controllerStatus = esp_bt_controller_get_status();
    }
  }

  if (controllerStatus != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (err != ESP_OK) {
      controllerStatus = esp_bt_controller_get_status();
      Serial.printf("[K380] controller enable returned %s, status now=%d\n",
                    esp_err_to_name(err),
                    static_cast<int>(controllerStatus));
      if (err != ESP_ERR_INVALID_STATE || controllerStatus != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        setInitFailure("controller enable", err);
        return false;
      }
    } else {
      controllerStatus = esp_bt_controller_get_status();
    }
  }

  esp_bluedroid_status_t bluedroidStatus = esp_bluedroid_get_status();
  Serial.printf("[K380] bluedroid status before init=%d\n", static_cast<int>(bluedroidStatus));

  if (bluedroidStatus == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
    esp_bluedroid_config_t bluedroidConfig = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroidConfig.ssp_en = true;
    esp_err_t err = esp_bluedroid_init_with_cfg(&bluedroidConfig);
    if (err != ESP_OK) {
      bluedroidStatus = esp_bluedroid_get_status();
      Serial.printf("[K380] bluedroid init returned %s, status now=%d\n",
                    esp_err_to_name(err),
                    static_cast<int>(bluedroidStatus));
      if (err != ESP_ERR_INVALID_STATE || bluedroidStatus == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        setInitFailure("bluedroid init", err);
        return false;
      }
    } else {
      bluedroidStatus = esp_bluedroid_get_status();
    }
  }

  if (bluedroidStatus != ESP_BLUEDROID_STATUS_ENABLED) {
    esp_err_t err = esp_bluedroid_enable();
    if (err != ESP_OK) {
      bluedroidStatus = esp_bluedroid_get_status();
      Serial.printf("[K380] bluedroid enable returned %s, status now=%d\n",
                    esp_err_to_name(err),
                    static_cast<int>(bluedroidStatus));
      if (err != ESP_ERR_INVALID_STATE || bluedroidStatus != ESP_BLUEDROID_STATUS_ENABLED) {
        setInitFailure("bluedroid enable", err);
        return false;
      }
    } else {
      bluedroidStatus = esp_bluedroid_get_status();
    }
  }

  esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
  esp_err_t err = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
  if (err != ESP_OK) {
    setInitFailure("security param", err);
    Serial.printf("[K380] GAP security param failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_bt_pin_type_t pinType = ESP_BT_PIN_TYPE_VARIABLE;
  esp_bt_pin_code_t pinCode = {0};
  err = esp_bt_gap_set_pin(pinType, 0, pinCode);
  if (err != ESP_OK) {
    setInitFailure("gap set pin", err);
    Serial.printf("[K380] GAP set pin failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = esp_bt_gap_register_callback(gapCallback);
  if (err != ESP_OK) {
    setInitFailure("gap callback", err);
    Serial.printf("[K380] GAP callback register failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = esp_bt_gap_set_device_name("biscuit-k380-probe");
  if (err != ESP_OK) {
    setInitFailure("gap device name", err);
    Serial.printf("[K380] GAP set device name failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  if (err != ESP_OK) {
    setInitFailure("gap scan mode", err);
    Serial.printf("[K380] GAP set scan mode failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = esp_ble_gap_register_callback(bleGapCallback);
  if (err != ESP_OK) {
    setInitFailure("ble gap callback", err);
    Serial.printf("[K380] BLE GAP callback register failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);
  if (err != ESP_OK) {
    setInitFailure("gattc callback", err);
    Serial.printf("[K380] BLE GATTC callback register failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_ble_auth_req_t bleAuthReq = ESP_LE_AUTH_REQ_SC_MITM_BOND;
  esp_ble_io_cap_t bleIoCap = ESP_IO_CAP_IO;
  uint8_t bleInitKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t bleRespKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t bleKeySize = 16;
  err = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &bleAuthReq, sizeof(bleAuthReq));
  if (err != ESP_OK) {
    setInitFailure("ble auth mode", err);
    Serial.printf("[K380] BLE auth mode failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &bleIoCap, sizeof(bleIoCap));
  if (err != ESP_OK) {
    setInitFailure("ble io cap", err);
    Serial.printf("[K380] BLE IO cap failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &bleInitKey, sizeof(bleInitKey));
  if (err != ESP_OK) {
    setInitFailure("ble init key", err);
    Serial.printf("[K380] BLE init key failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &bleRespKey, sizeof(bleRespKey));
  if (err != ESP_OK) {
    setInitFailure("ble response key", err);
    Serial.printf("[K380] BLE response key failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &bleKeySize, sizeof(bleKeySize));
  if (err != ESP_OK) {
    setInitFailure("ble key size", err);
    Serial.printf("[K380] BLE key size failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_hidh_config_t hidConfig = {
      .callback = hidCallback,
      .event_stack_size = 4096,
      .callback_arg = nullptr,
  };
  err = esp_hidh_init(&hidConfig);
  if (err != ESP_OK) {
    setInitFailure("esp_hidh_init", err);
    Serial.printf("[K380] esp_hidh_init failed: %s\n", esp_err_to_name(err));
    return false;
  }

  s_btReady = true;
  s_initFailure[0] = '\0';
  printBonds();
  if (s_hidReady) {
    startDiscovery(true);
  }
  return true;
}

void printHelp() {
  Serial.println("[K380] commands:");
  Serial.println("[K380]   help          - print this help");
  Serial.println("[K380]   status        - print probe state");
  Serial.println("[K380]   scan          - restart Classic BT + BLE discovery");
  Serial.println("[K380]   bonds         - list Classic BT and BLE bonded devices");
  Serial.println("[K380]   clear-bonds   - explicit cleanup if pairing state is stale");
  Serial.println("[K380]   close         - close current HID device");
}

void handleCommand(const String &rawCommand) {
  String command = rawCommand;
  command.trim();
  command.toLowerCase();
  if (command.isEmpty()) {
    return;
  }

  if (command == "help" || command == "?") {
    printHelp();
  } else if (command == "status") {
    printStatus();
  } else if (command == "scan") {
    cancelActiveScans();
    s_nextScanAllowedMs = 0;
    startDiscovery(true);
  } else if (command == "bonds") {
    printBonds();
  } else if (command == "clear-bonds") {
    clearBonds();
  } else if (command == "close") {
    if (s_device != nullptr) {
      Serial.println("[K380] closing HID device");
      esp_hidh_dev_close(s_device);
    } else {
      Serial.println("[K380] no HID device is open");
    }
  } else {
    Serial.printf("[K380] unknown command: %s\n", command.c_str());
    printHelp();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("[K380] Biscuit Logitech K380 HID host probe (Classic BT + BLE)");
  Serial.println("[K380] Pairing flow: hold K380 slot 1 until it blinks; type any PASSKEY shown here on the keyboard.");
  Serial.println("[K380] This build does not run the reader firmware.");
  printHelp();

  esp_log_level_set("*", ESP_LOG_INFO);
  if (!initNvs() || !initBluetooth()) {
    Serial.println("[K380] Bluetooth init failed; probe stopped");
  }
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand(Serial.readStringUntil('\n'));
  }

  const uint32_t now = millis();
  if (now - s_lastStatusMs >= kStatusIntervalMs) {
    s_lastStatusMs = now;
    printStatus();
  }

  if (s_btReady && s_hidReady && !scanBusy() && !s_connecting && !s_connected && !s_connectQueued &&
      now - s_lastScanFinishedMs >= kScanRestartIntervalMs) {
    startDiscovery();
  }

  delay(50);
}
