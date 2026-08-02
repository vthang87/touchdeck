#include "app_manager.h"

#include <esp_system.h>

#include "app_config.h"
#include "version.h"
#include "board_config.h"
#include "storage/settings_store.h"
#include "system/system_status.h"
#include "app/input_queue.h"
#include "ui/ui_manager.h"
#include "ui/screens/home_grid_screen.h"
#include "ble/ble_manager.h"
#include "usb/usb_hid.h"
#include "wifi/wifi_manager.h"
#include "ota/ota_manager.h"

AppManager appManager;

void AppManager::begin() {
  Serial.begin(APP_SERIAL_BAUD);
  delay(200);

  Serial.println();
  Serial.println("========================================");
  Serial.printf("[BOOT] Firmware %s\n", FIRMWARE_VERSION);
  Serial.printf("[BOOT] Hardware %s\n", HARDWARE_REVISION);
  Serial.printf("[BOOT] Built %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
  Serial.printf("[BOOT] Protocol %d\n", PROTOCOL_VERSION);
  Serial.printf("[BOOT] Reset reason %u\n", static_cast<unsigned>(esp_reset_reason()));
  Serial.printf("[BOOT] Chip model %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("[BOOT] Flash %u MB  PSRAM %u MB\n", ESP.getFlashChipSize() / (1024 * 1024),
                ESP.getPsramSize() / (1024 * 1024));
  Serial.println("========================================");

  systemStatus.begin();
  settingsStore.begin();
  inputQueue.begin();

  DeviceSettings settings = settingsStore.load();
  if (settings.device_name.isEmpty()) {
    settings.device_name = APP_DEFAULT_DEVICE_NAME;
  }
  if (settings.ble_name.isEmpty()) {
    settings.ble_name = settings.device_name;
  }
  if (settings.ota_password.isEmpty()) {
    settings.ota_password = APP_DEFAULT_OTA_PASSWORD;
  }
  if (settings.hostname.isEmpty()) {
    settings.hostname = "touchdeck";
  }

  // A config that crashes during Wi-Fi bring-up would otherwise loop forever.
  const uint8_t boot_try = settingsStore.bumpBootAttempts();
  if (boot_try >= 3 && settings.provisioned) {
    Serial.printf("[BOOT] %u failed boots — ignoring saved Wi-Fi, starting AP\n", boot_try);
    settings.provisioned = false;
  }

  if (!uiManagerBegin()) {
    Serial.println("[BOOT] UI init failed — continuing headless");
  }

  const String ble_name = settings.ble_name.length() ? settings.ble_name : settings.device_name;
  if (!usbHidBegin(settings.device_name)) {
    Serial.println("[BOOT] USB HID not active (touch uses GPIO19/20)");
  }
  if (!settings.ble_enabled) {
    Serial.println("[BOOT] Bluetooth disabled in settings");
    systemStatus.setBle(SystemBleState::Stopped);
  } else if (bleManagerBegin(ble_name, settings.ble_pair_mode)) {
    gatt_started_ = true;
  } else {
    Serial.println("[BOOT] BLE GATT init failed");
  }

  wifiManager.begin(settings);
  systemStatus.setVolume(homeGridScreenGetVolume(), homeGridScreenIsMuted());
  Serial.println("[BOOT] Ready");
}

void AppManager::processInput() {
  InputEvent ev;
  while (inputQueue.pop(ev)) {
    switch (ev.action) {
      case InputAction::VolumeUp:
      case InputAction::VolumeDown:
      case InputAction::MuteToggle:
      case InputAction::PlayPause:
      case InputAction::NextTrack:
      case InputAction::PrevTrack:
      case InputAction::LaunchApp:
        // v4: companion owns all host actions via GATT action_id.
        if (!gatt_started_ || !bleManagerIsConnected()) {
          Serial.printf("[APP] no GATT companion — action_id=%s ignored\n",
                        ev.action_id[0] ? ev.action_id : ev.tile_id);
          break;
        }
        bleManagerNotifyTilePress(ev);
        systemStatus.setVolume(homeGridScreenGetVolume(), homeGridScreenIsMuted());
        break;
      case InputAction::Restart:
        Serial.println("[APP] Restart requested");
        delay(200);
        ESP.restart();
        break;
      case InputAction::GetStatus:
        if (gatt_started_) {
          bleManagerNotifyStatus();
        }
        break;
      default:
        break;
    }
  }
}

void AppManager::maybeStartOta() {
  if (ota_started_ || !wifiManager.isConnected()) {
    return;
  }
  DeviceSettings settings = settingsStore.load();
  otaManager.begin(settings);
  ota_started_ = true;
  if (gatt_started_) {
    bleManagerNotifyEvent("{\"type\":\"wifi_connected\"}");
  }
}

void AppManager::loop() {
  uiManagerTick();
  wifiManager.tick();
  maybeStartOta();
  otaManager.tick();
  usbHidTick();
  if (gatt_started_) {
    bleManagerTick();
  }
  processInput();
  systemStatus.tick();

  if (!boot_marked_stable_ && millis() > 15000) {
    boot_marked_stable_ = true;
    settingsStore.clearBootAttempts();
    Serial.println("[BOOT] Marked stable");
  }

  if (millis() - last_status_ms_ > 30000) {
    last_status_ms_ = millis();
    const SystemSnapshot s = systemStatus.snapshot();
    Serial.printf("[SYS] wifi=%s ble=%s ota=%s heap=%u ip=%s\n", wifiStateName(s.wifi), bleStateName(s.ble),
                  otaStateName(s.ota), s.free_heap, s.ip.c_str());
  }
}
