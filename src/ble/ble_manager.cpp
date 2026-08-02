#include "ble_manager.h"

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>
#include <ArduinoJson.h>

#include "system/system_status.h"
#include "version.h"
#include "board_config.h"
#include "app/input_queue.h"
#include "ui/screens/home_grid_screen.h"
#include "grid_config.h"

// Custom TouchDeck service (not official UUID — local use)
static NimBLEUUID kServiceUuid("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kCommandUuid("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kEventUuid("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kStatusUuid("6E400004-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID kInfoUuid("6E400005-B5A3-F393-E0A9-E50E24DCCA9E");

static NimBLECharacteristic* s_event = nullptr;
static NimBLECharacteristic* s_status = nullptr;
static bool s_ready = false;

static String buildStatusJson() {
  const SystemSnapshot snap = systemStatus.snapshot();
  char buf[288];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"status\",\"wifi\":%s,\"ip\":\"%s\",\"uptime\":%u,\"heap\":%u,\"vol\":%d,\"mute\":%s,\"fw\":\"%s\","
           "\"protocol\":%d}",
           snap.wifi == SystemWifiState::Connected ? "true" : "false", snap.ip.c_str(), snap.uptime_sec,
           snap.free_heap, homeGridScreenGetVolume(), homeGridScreenIsMuted() ? "true" : "false", FIRMWARE_VERSION,
           PROTOCOL_VERSION);
  return String(buf);
}

static String buildInfoJson() {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"model\":\"%s\",\"fw\":\"%s\",\"hw\":\"%s\",\"protocol\":%d}", BOARD_MODEL_NAME, FIRMWARE_VERSION,
           HARDWARE_REVISION, PROTOCOL_VERSION);
  return String(buf);
}

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    const std::string value = c->getValue();
    const String cmd(value.c_str());
    Serial.printf("[GATT] command: %s\n", cmd.c_str());

    // Prefer JSON ops; keep legacy substring match for restart/get_status.
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
      }
    }
    if (cmd.indexOf("restart") >= 0) {
      inputQueue.push(InputAction::Restart);
    } else if (cmd.indexOf("get_status") >= 0) {
      bleManagerNotifyStatus();
    }
  }
};

bool bleManagerBegin() {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (!server) {
    Serial.println("[GATT] No BLE server — start HID first");
    return false;
  }

  NimBLEService* service = server->createService(kServiceUuid);

  NimBLECharacteristic* command = service->createCharacteristic(
      kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  command->setCallbacks(new CommandCallbacks());

  s_event = service->createCharacteristic(kEventUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  s_status =
      service->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* info = service->createCharacteristic(kInfoUuid, NIMBLE_PROPERTY::READ);

  s_status->setValue(buildStatusJson().c_str());
  info->setValue(buildInfoJson().c_str());
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(service->getUUID());
  advertising->start();

  s_ready = true;
  bleManagerNotifyEvent("{\"type\":\"device_ready\"}");
  Serial.println("[GATT] TouchDeck service ready");
  return true;
}

void bleManagerTick() {
  // Periodic status refresh can be added later.
}

void bleManagerNotifyStatus() {
  if (!s_ready || !s_status) {
    return;
  }
  const String json = buildStatusJson();
  s_status->setValue(json.c_str());
  s_status->notify();
}

void bleManagerNotifyEvent(const String& json) {
  if (!s_ready || !s_event) {
    return;
  }
  s_event->setValue(json.c_str());
  s_event->notify();
}

void bleManagerNotifyTilePress(const InputEvent& ev) {
  if (!s_ready) {
    return;
  }
  char buf[256];
  const char* kind = appTargetKindToString(ev.app_kind);
  snprintf(buf, sizeof(buf),
           "{\"type\":\"tile_press\",\"id\":\"%s\",\"t\":%u,\"target\":{\"kind\":\"%s\",\"value\":\"%s\"}}",
           ev.tile_id, ev.timestamp_ms, kind, ev.app_value);
  bleManagerNotifyEvent(String(buf));
  Serial.printf("[GATT] tile_press %s -> %s:%s\n", ev.tile_id, kind, ev.app_value);
}
