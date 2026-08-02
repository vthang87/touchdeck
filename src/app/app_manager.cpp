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
#include "ble/ble_hid.h"
#include "ble/ble_manager.h"
#include "usb/usb_hid.h"
#include "wifi/wifi_manager.h"
#include "net/event_server.h"
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
  } else if (!bleHidBegin(ble_name, settings.ble_pair_mode)) {
    Serial.println("[BOOT] BLE HID init failed");
  } else if (bleManagerBegin()) {
    gatt_started_ = true;
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
        {
          // Volume prefers BLE HID when paired (macOS shows the system HUD).
          // Without BLE, fall back to the companion's exact 3% Wi-Fi step.
          const bool is_volume =
              ev.action == InputAction::VolumeUp || ev.action == InputAction::VolumeDown;
          const bool companion_adjusts_volume =
              is_volume && !bleHidIsConnected() && eventServerClientCount() > 0;
          bool sent_hid = false;
          if (companion_adjusts_volume) {
            Serial.println("[VOL] companion fine adjustment (3%) — BLE not connected");
          } else {
            usbHidSend(ev.action);
            if (bleHidIsConnected()) {
              bleHidSend(ev.action);
              sent_hid = true;
              if (is_volume) {
                Serial.println("[VOL] BLE HID (system HUD)");
              }
            } else if (usbHidIsReady()) {
              sent_hid = true;
            } else {
              Serial.println("[HID] no USB/BLE host — media action ignored");
            }
          }
          systemStatus.setVolume(homeGridScreenGetVolume(), homeGridScreenIsMuted());
          // The on-screen level is only a guess until the companion reports the host value.
          eventServerBroadcastMediaPress(ev, sent_hid);
        }
        if (gatt_started_) {
          bleManagerNotifyStatus();
        }
        break;
      case InputAction::LaunchApp:
        // macOS reserves BLE HID peripherals, so the companion listens over Wi-Fi.
        if (eventServerClientCount() > 0) {
          eventServerBroadcastTilePress(ev);
        } else {
          Serial.printf("[APP] no companion connected — %s not launched\n", ev.tile_id);
        }
        if (gatt_started_) {
          bleManagerNotifyTilePress(ev);
        }
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

void AppManager::maybeStartEventServer() {
  if (event_server_started_) {
    return;
  }
  if (!wifiManager.isConnected() && !wifiManager.isApMode()) {
    return;
  }
  if (eventServerBegin()) {
    event_server_started_ = true;
    Serial.printf("[WS] companion endpoint ws://%s:81/\n", wifiManager.ipAddress().c_str());
  }
}

void AppManager::loop() {
  uiManagerTick();
  wifiManager.tick();
  maybeStartEventServer();
  eventServerTick();
  maybeStartOta();
  otaManager.tick();
  usbHidTick();
  bleHidTick();
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
