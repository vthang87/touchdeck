#include "ble_manager.h"

#include <esp_mac.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>
#include <ArduinoJson.h>

#include "system/system_status.h"
#include "version.h"
#include "board_config.h"
#include "app/input_queue.h"
#include "ui/screens/home_grid_screen.h"
#include "ui/screens/media_screen.h"
#include "ui/ui_manager.h"
#include "display/display_driver.h"
#include "grid_config.h"

static NimBLEUUID kServiceUuid("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kCommandUuid("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kEventUuid("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kStatusUuid("6E400004-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kInfoUuid("6E400005-B5A3-F393-E0A9-E50E24DCCA9E");

static NimBLECharacteristic* s_event = nullptr;
static NimBLECharacteristic* s_status = nullptr;
static NimBLEServer* s_server = nullptr;
static String s_name;
static bool s_ready = false;
static bool s_connected = false;
static bool s_pair_mode = true;

static String buildStatusJson() {
  const SystemSnapshot snap = systemStatus.snapshot();
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"status\",\"wifi\":%s,\"ip\":\"%s\",\"uptime\":%u,\"heap\":%u,\"vol\":%d,\"mute\":%s,"
           "\"fw\":\"%s\",\"protocol\":%d,\"gatt\":true}",
           snap.wifi == SystemWifiState::Connected ? "true" : "false", snap.ip.c_str(), snap.uptime_sec,
           snap.free_heap, homeGridScreenGetVolume(), homeGridScreenIsMuted() ? "true" : "false", FIRMWARE_VERSION,
           PROTOCOL_VERSION);
  return String(buf);
}

static String buildInfoJson() {
  char buf[224];
  snprintf(buf, sizeof(buf),
           "{\"model\":\"%s\",\"fw\":\"%s\",\"hw\":\"%s\",\"protocol\":%d,\"gatt\":true}", BOARD_MODEL_NAME,
           FIRMWARE_VERSION, HARDWARE_REVISION, PROTOCOL_VERSION);
  return String(buf);
}

static void startAdvertisingLocked() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising) {
    return;
  }
  advertising->start();
  systemStatus.setBle(SystemBleState::Advertising);
  Serial.println("[GATT] pairing mode ON — advertising");
}

static void stopAdvertisingLocked() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising) {
    advertising->stop();
  }
  if (s_connected) {
    systemStatus.setBle(SystemBleState::Connected);
  } else {
    systemStatus.setBle(SystemBleState::Stopped);
  }
  Serial.println("[GATT] pairing mode OFF — not discoverable");
}

class GattServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override {
    s_connected = true;
    systemStatus.setBle(SystemBleState::Connected);
    Serial.printf("[GATT] Central connected (peers=%u)\n", server ? server->getConnectedCount() : 0);
    if (!s_pair_mode) {
      NimBLEDevice::stopAdvertising();
    }
    // Prefer larger payloads for JSON tile_press.
    NimBLEDevice::setMTU(517);
    bleManagerNotifyEvent("{\"type\":\"hello\",\"fw\":\"" FIRMWARE_VERSION "\",\"protocol\":4,\"gatt\":true}");
  }

  void onDisconnect(NimBLEServer* /*server*/) override {
    s_connected = false;
    if (s_pair_mode) {
      systemStatus.setBle(SystemBleState::Advertising);
      Serial.println("[GATT] Central disconnected — advertising");
      NimBLEDevice::startAdvertising();
    } else {
      systemStatus.setBle(SystemBleState::Stopped);
      Serial.println("[GATT] Central disconnected — pair mode off");
    }
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    const std::string value = c->getValue();
    const String cmd(value.c_str());
    Serial.printf("[GATT] command: %s\n", cmd.c_str());

    if (cmd.startsWith("{")) {
      JsonDocument doc;
      if (deserializeJson(doc, cmd) == DeserializationError::Ok) {
        const char* op = doc["op"] | "";
        if (strcmp(op, "restart") == 0) {
          inputQueue.push(InputAction::Restart);
          return;
        }
        if (strcmp(op, "get_status") == 0) {
          bleManagerNotifyStatus();
          return;
        }
        if (strcmp(op, "ping") == 0) {
          bleManagerNotifyEvent("{\"type\":\"pong\"}");
          return;
        }
        if (strcmp(op, "volume") == 0) {
          const int level = doc["level"] | -1;
          const bool muted = doc["muted"] | false;
          if (level >= 0 && level <= 100) {
            Serial.printf("[BLE] volume %d%% muted=%d\n", level, muted ? 1 : 0);
            homeGridScreenSetVolume(level, muted);
            mediaScreenSetVolume(level, muted);
            bleManagerNotifyStatus();
          }
          return;
        }
        if (strcmp(op, "now_playing") == 0) {
          const char* title = doc["title"] | "";
          const char* artist = doc["artist"] | "";
          const char* app = doc["app"] | "";
          const bool playing = doc["playing"] | false;
          const uint32_t pos_ms = doc["pos_ms"] | 0;
          const uint32_t dur_ms = doc["dur_ms"] | 0;
          const uint16_t rate_x100 = static_cast<uint16_t>(doc["rate_x100"] | 100);
          mediaScreenSetNowPlaying(title, artist, playing, pos_ms, dur_ms, app, rate_x100);
          mediaScreenSetLinked(true);
          return;
        }
        if (strcmp(op, "page") == 0) {
          const int index = doc["index"] | 0;
          if (index >= 0 && index < DECK_PAGE_MAX) {
            uiManagerSetPage(static_cast<uint8_t>(index));
          }
          return;
        }
        if (strcmp(op, "pages") == 0) {
          const int count = doc["count"] | DECK_PAGE_MIN;
          uiManagerSetPageCount(static_cast<uint8_t>(count));
          return;
        }
        if (strcmp(op, "highlight") == 0) {
          const char* source = doc["source"] | "";
          const bool on = doc["on"] | false;
          homeGridScreenSetApprovalHighlight(source, on);
          return;
        }
        if (strcmp(op, "brightness") == 0) {
          const int pct = doc["pct"] | -1;
          if (pct >= 0 && pct <= 100) {
            displayDriverSetBacklight(static_cast<uint8_t>(pct));
          }
          return;
        }
        if (strcmp(op, "enter_ota") == 0) {
          bleManagerNotifyEvent("{\"type\":\"ota_ready\"}");
          return;
        }
      }
    }
    if (cmd.indexOf("restart") >= 0) {
      inputQueue.push(InputAction::Restart);
    } else if (cmd.indexOf("get_status") >= 0) {
      bleManagerNotifyStatus();
    }
  }
};

bool bleManagerBegin(const String& device_name, bool pair_mode) {
  s_name = device_name;
  s_pair_mode = pair_mode;
  if (s_name.isEmpty()) {
    s_name = "TouchDeck";
  }

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
  if (!s_name.endsWith(suffix) && s_name.indexOf('-') < 0) {
    s_name += suffix;
  }

  NimBLEDevice::init(s_name.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(517);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new GattServerCallbacks());

  NimBLEService* service = s_server->createService(kServiceUuid);

  NimBLECharacteristic* command = service->createCharacteristic(
      kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 512);
  command->setCallbacks(new CommandCallbacks());

  // Explicit 512-byte value buffer — default can be too small for JSON events.
  s_event =
      service->createCharacteristic(kEventUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ, 512);
  s_status =
      service->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512);
  NimBLECharacteristic* info =
      service->createCharacteristic(kInfoUuid, NIMBLE_PROPERTY::READ, 256);

  s_status->setValue(buildStatusJson().c_str());
  info->setValue(buildInfoJson().c_str());
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(s_name.c_str());
  advertising->addServiceUUID(service->getUUID());
  advertising->setScanResponse(true);

  s_ready = true;
  if (s_pair_mode) {
    advertising->start();
    systemStatus.setBle(SystemBleState::Advertising);
    Serial.printf("[GATT] advertising as %s (pair mode ON)\n", s_name.c_str());
  } else {
    systemStatus.setBle(SystemBleState::Stopped);
    Serial.printf("[GATT] ready as %s (pair mode OFF)\n", s_name.c_str());
  }
  bleManagerNotifyEvent("{\"type\":\"device_ready\"}");
  return true;
}

void bleManagerTick() {}

bool bleManagerIsConnected() { return s_connected; }

bool bleManagerIsStarted() { return s_ready; }

bool bleManagerPairMode() { return s_pair_mode; }

void bleManagerSetPairMode(bool enabled) {
  if (!s_ready) {
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

String bleManagerDeviceName() { return s_name; }

void bleManagerNotifyStatus() {
  if (!s_ready || !s_status) {
    return;
  }
  const String json = buildStatusJson();
  s_status->setValue(reinterpret_cast<const uint8_t*>(json.c_str()), json.length());
  s_status->notify();
}

void bleManagerNotifyEvent(const String& json) {
  if (!s_ready || !s_event) {
    return;
  }
  s_event->setValue(reinterpret_cast<const uint8_t*>(json.c_str()), json.length());
  s_event->notify();
  Serial.printf("[GATT] notify event len=%u: %s\n", static_cast<unsigned>(json.length()), json.c_str());
}

void bleManagerNotifyTilePress(const InputEvent& ev) {
  if (!s_ready) {
    return;
  }
  const char* action_id = ev.action_id[0] ? ev.action_id : ev.tile_id;
  char buf[192];
  snprintf(buf, sizeof(buf), "{\"event\":\"tile_press\",\"action_id\":\"%s\",\"t\":%u,\"id\":\"%s\"}", action_id,
           ev.timestamp_ms, ev.tile_id);
  bleManagerNotifyEvent(String(buf));
}
