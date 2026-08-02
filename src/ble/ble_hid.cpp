#include "ble_hid.h"

#include <esp_mac.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>

#include "system/system_status.h"

namespace {

constexpr uint16_t kUsageVolumeUp = 0x00E9;
constexpr uint16_t kUsageVolumeDown = 0x00EA;
constexpr uint16_t kUsageMute = 0x00E2;
constexpr uint16_t kUsagePlayPause = 0x00CD;
constexpr uint16_t kUsageNext = 0x00B5;
constexpr uint16_t kUsagePrevious = 0x00B6;

// Consumer Control — 16-bit usage array (Report ID 1)
const uint8_t kHidReportMap[] = {
    0x05, 0x0C,        // Usage Page (Consumer Devices)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Ary,Abs)
    0xC0,              // End Collection
};

NimBLEHIDDevice* s_hid = nullptr;
NimBLECharacteristic* s_input = nullptr;
NimBLEServer* s_server = nullptr;
String s_name;
bool s_connected = false;
bool s_started = false;
bool s_pair_mode = true;

void startAdvertisingLocked() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising) {
    return;
  }
  advertising->start();
  systemStatus.setBle(SystemBleState::Advertising);
  Serial.println("[BLE] pairing mode ON — advertising");
}

void stopAdvertisingLocked() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising) {
    advertising->stop();
  }
  if (s_connected) {
    systemStatus.setBle(SystemBleState::Connected);
  } else {
    systemStatus.setBle(SystemBleState::Stopped);
  }
  Serial.println("[BLE] pairing mode OFF — not discoverable");
}

class HidServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/) override {
    s_connected = true;
    systemStatus.setBle(SystemBleState::Connected);
    Serial.println("[BLE] Client connected");
    // Stay discoverable only while pair mode is explicitly on.
    if (!s_pair_mode) {
      NimBLEDevice::stopAdvertising();
    }
  }

  void onDisconnect(NimBLEServer* /*server*/) override {
    s_connected = false;
    if (s_pair_mode) {
      systemStatus.setBle(SystemBleState::Advertising);
      Serial.println("[BLE] Client disconnected — advertising");
      NimBLEDevice::startAdvertising();
    } else {
      systemStatus.setBle(SystemBleState::Stopped);
      Serial.println("[BLE] Client disconnected — pair mode off, not advertising");
    }
  }
};

void sendUsage(uint16_t usage) {
  if (!s_input) {
    return;
  }
  uint8_t press[2] = {static_cast<uint8_t>(usage & 0xFF), static_cast<uint8_t>((usage >> 8) & 0xFF)};
  uint8_t release[2] = {0, 0};
  s_input->setValue(press, sizeof(press));
  s_input->notify();
  delay(20);
  s_input->setValue(release, sizeof(release));
  s_input->notify();
}

}  // namespace

bool bleHidBegin(const String& device_name, bool pair_mode) {
  s_name = device_name;
  s_pair_mode = pair_mode;
  if (s_name.isEmpty()) {
    s_name = "TouchDeck";
  }

  // Suffix from MAC for uniqueness
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
  if (!s_name.endsWith(suffix) && s_name.indexOf('-') < 0) {
    s_name += suffix;
  }

  NimBLEDevice::init(s_name.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, true, true);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new HidServerCallbacks());

  s_hid = new NimBLEHIDDevice(s_server);
  s_input = s_hid->inputReport(1);
  s_hid->manufacturer("TouchDeck");
  s_hid->pnp(0x02, 0x05AC, 0x820A, 0x0001);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap(const_cast<uint8_t*>(kHidReportMap), sizeof(kHidReportMap));
  s_hid->startServices();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->setName(s_name.c_str());
  advertising->addServiceUUID(s_hid->hidService()->getUUID());
  advertising->setScanResponse(true);

  s_started = true;
  if (s_pair_mode) {
    advertising->start();
    systemStatus.setBle(SystemBleState::Advertising);
    Serial.printf("[BLE] HID advertising as %s (pair mode ON)\n", s_name.c_str());
  } else {
    systemStatus.setBle(SystemBleState::Stopped);
    Serial.printf("[BLE] HID ready as %s (pair mode OFF — not discoverable)\n", s_name.c_str());
  }
  return true;
}

void bleHidTick() {
  // Advertising restart handled in disconnect callback.
}

bool bleHidIsConnected() { return s_connected; }

bool bleHidIsStarted() { return s_started; }

bool bleHidPairMode() { return s_pair_mode; }

void bleHidSetPairMode(bool enabled) {
  if (!s_started) {
    s_pair_mode = enabled;
    return;
  }
  if (s_pair_mode == enabled) {
    return;
  }
  s_pair_mode = enabled;
  if (enabled) {
    startAdvertisingLocked();
  } else {
    stopAdvertisingLocked();
  }
}

void bleHidSend(InputAction action) {
  if (!s_connected) {
    Serial.println("[BLE] skip — not connected");
    return;
  }
  switch (action) {
    case InputAction::VolumeUp:
      Serial.println("[BLE] volume_up");
      sendUsage(kUsageVolumeUp);
      break;
    case InputAction::VolumeDown:
      Serial.println("[BLE] volume_down");
      sendUsage(kUsageVolumeDown);
      break;
    case InputAction::MuteToggle:
      Serial.println("[BLE] mute");
      sendUsage(kUsageMute);
      break;
    case InputAction::PlayPause:
      Serial.println("[BLE] play_pause");
      sendUsage(kUsagePlayPause);
      break;
    case InputAction::NextTrack:
      Serial.println("[BLE] next");
      sendUsage(kUsageNext);
      break;
    case InputAction::PrevTrack:
      Serial.println("[BLE] previous");
      sendUsage(kUsagePrevious);
      break;
    default:
      break;
  }
}

String bleHidDeviceName() { return s_name; }

NimBLEServer* bleHidServer() { return s_server; }
