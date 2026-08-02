#include "event_server.h"

#include <ArduinoJson.h>
#include <WebSocketsServer.h>

#include "version.h"
#include "board_config.h"
#include "grid_config.h"
#include "ui/screens/home_grid_screen.h"
#include "ui/screens/notification_overlay.h"

static WebSocketsServer* s_ws = nullptr;
static bool s_running = false;
static uint8_t s_clients = 0;

static String buildHelloJson() {
  char buf[192];
  snprintf(buf, sizeof(buf), "{\"type\":\"hello\",\"model\":\"%s\",\"fw\":\"%s\",\"protocol\":%d}",
           BOARD_MODEL_NAME, FIRMWARE_VERSION, PROTOCOL_VERSION);
  return String(buf);
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      ++s_clients;
      const IPAddress ip = s_ws->remoteIP(num);
      Serial.printf("[WS] client %u connected from %s (total %u)\n", num, ip.toString().c_str(), s_clients);
      String hello = buildHelloJson();
      s_ws->sendTXT(num, hello);
      break;
    }
    case WStype_DISCONNECTED:
      if (s_clients > 0) {
        --s_clients;
      }
      Serial.printf("[WS] client %u disconnected (total %u)\n", num, s_clients);
      break;
    case WStype_TEXT: {
      if (length == 0 || length > 512) {
        break;
      }
      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
        break;
      }
      const char* op = doc["op"] | doc["type"] | "";
      if (strcmp(op, "ping") == 0) {
        String pong = "{\"type\":\"pong\"}";
        s_ws->sendTXT(num, pong);
        break;
      }
      // The companion owns the real host volume; the board only mirrors it.
      if (strcmp(op, "volume") == 0) {
        const int level = doc["level"] | -1;
        const bool muted = doc["muted"] | false;
        if (level >= 0 && level <= 100) {
          homeGridScreenSetVolume(level, muted);
          Serial.printf("[WS] host volume %d%% muted=%d\n", level, muted ? 1 : 0);
        }
        break;
      }
      if (strcmp(op, "notification") == 0) {
        const char* id = doc["id"] | "";
        const char* source = doc["source"] | id;
        const char* title = doc["title"] | "Approval";
        const char* body = doc["body"] | "Waiting for approval";
        if (id[0]) {
          notificationOverlaySet(id, source, title, body);
          homeGridScreenSetApprovalHighlight(source, true);
          Serial.printf("[WS] notification %s (%s)\n", id, source);
        }
        break;
      }
      if (strcmp(op, "notification_clear") == 0) {
        const char* id = doc["id"] | "";
        if (id[0]) {
          notificationOverlayClear(id);
          homeGridScreenSetApprovalHighlight(id, false);
          Serial.printf("[WS] notification_clear %s\n", id);
        }
        break;
      }
      if (strcmp(op, "notification_clear_all") == 0 || doc["notification_clear_all"] == true) {
        notificationOverlayClearAll();
        homeGridScreenClearApprovalHighlights();
        Serial.println("[WS] notification_clear_all");
        break;
      }
      break;
    }
    default:
      break;
  }
}

bool eventServerBegin(uint16_t port) {
  if (s_running) {
    return true;
  }
  if (!s_ws) {
    s_ws = new WebSocketsServer(port);
  }
  s_ws->begin();
  s_ws->onEvent(onWsEvent);
  s_running = true;
  Serial.printf("[WS] Event server on port %u\n", port);
  return true;
}

void eventServerTick() {
  if (s_running && s_ws) {
    s_ws->loop();
  }
}

void eventServerBroadcast(const String& json) {
  if (!s_running || !s_ws || s_clients == 0) {
    return;
  }
  String payload = json;  // WebSocketsServer takes a non-const reference.
  s_ws->broadcastTXT(payload);
}

void eventServerBroadcastTilePress(const InputEvent& ev) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"tile_press\",\"id\":\"%s\",\"t\":%u,\"target\":{\"kind\":\"%s\",\"value\":\"%s\"}}",
           ev.tile_id, ev.timestamp_ms, appTargetKindToString(ev.app_kind), ev.app_value);
  eventServerBroadcast(String(buf));
  Serial.printf("[WS] tile_press %s -> %s:%s (%u client)\n", ev.tile_id,
                appTargetKindToString(ev.app_kind), ev.app_value, s_clients);
}

void eventServerBroadcastMediaPress(const InputEvent& ev, bool handled) {
  const char* action = "";
  switch (ev.action) {
    case InputAction::VolumeUp: action = "volume_up"; break;
    case InputAction::VolumeDown: action = "volume_down"; break;
    case InputAction::MuteToggle: action = "mute"; break;
    case InputAction::PlayPause: action = "play_pause"; break;
    case InputAction::NextTrack: action = "next"; break;
    case InputAction::PrevTrack: action = "previous"; break;
    default: return;
  }
  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"media_press\",\"action\":\"%s\",\"handled\":%s,\"t\":%u}", action,
           handled ? "true" : "false", ev.timestamp_ms);
  eventServerBroadcast(String(buf));
}

uint8_t eventServerClientCount() { return s_clients; }

bool eventServerIsRunning() { return s_running; }
