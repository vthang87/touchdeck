#include "profile_store.h"

#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

ProfileStore profileStore;

bool ProfileStore::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
    return false;
  }
  ready_ = true;

  if (!load(current_)) {
    Serial.println("[FS] No grid — writing defaults");
    resetToDefault(current_);
  } else {
    Serial.printf("[FS] Grid loaded rev=%u %ux%u\n", current_.rev, current_.cols, current_.rows);
  }
  return true;
}

bool ProfileStore::serialize(const GridConfig& cfg, String& out_json) const {
  JsonDocument doc;
  doc["rev"] = cfg.rev;
  doc["cols"] = cfg.cols;
  doc["rows"] = cfg.rows;
  JsonArray tiles = doc["tiles"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.tile_count; ++i) {
    const GridTile& t = cfg.tiles[i];
    JsonObject o = tiles.add<JsonObject>();
    o["id"] = t.id;
    o["label"] = t.label;
    char color_hex[10];
    snprintf(color_hex, sizeof(color_hex), "#%06X", static_cast<unsigned>(t.color & 0xFFFFFF));
    o["color"] = color_hex;
    o["icon"] = t.icon;
    o["action"] = tileActionToString(t.action);
    if (t.action == TileAction::App) {
      JsonObject target = o["target"].to<JsonObject>();
      target["kind"] = appTargetKindToString(t.app_kind);
      target["value"] = t.app_value;
    }
  }
  if (measureJson(doc) > GRID_JSON_MAX_BYTES) {
    return false;
  }
  out_json = "";
  serializeJson(doc, out_json);
  return out_json.length() > 0 && out_json.length() <= GRID_JSON_MAX_BYTES;
}

bool ProfileStore::parse(const char* json, size_t len, GridConfig& out, char* err, size_t err_len) const {
  auto fail = [&](const char* msg) -> bool {
    if (err && err_len) {
      strncpy(err, msg, err_len - 1);
      err[err_len - 1] = '\0';
    }
    return false;
  };
  if (!json || len == 0 || len > GRID_JSON_MAX_BYTES) {
    return fail("payload too large or empty");
  }

  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    return fail("invalid json");
  }

  GridConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.rev = doc["rev"] | 1;
  cfg.cols = doc["cols"] | 0;
  cfg.rows = doc["rows"] | 0;
  JsonArray tiles = doc["tiles"].as<JsonArray>();
  if (tiles.isNull()) {
    return fail("tiles missing");
  }
  if (tiles.size() > GRID_MAX_TILES) {
    return fail("too many tiles");
  }
  cfg.tile_count = static_cast<uint8_t>(tiles.size());
  uint8_t i = 0;
  for (JsonObject o : tiles) {
    GridTile& t = cfg.tiles[i++];
    const char* id = o["id"] | "";
    const char* label = o["label"] | "";
    const char* icon = o["icon"] | "app";
    const char* action = o["action"] | "none";
    const char* color_s = o["color"] | "#1E293B";
    strncpy(t.id, id, GRID_ID_MAX);
    strncpy(t.label, label, GRID_LABEL_MAX);
    strncpy(t.icon, icon, GRID_ICON_MAX);
    t.action = tileActionFromString(action);
    if (color_s[0] == '#') {
      t.color = strtoul(color_s + 1, nullptr, 16);
    } else {
      t.color = strtoul(color_s, nullptr, 16);
    }
    if (t.action == TileAction::App) {
      JsonObject target = o["target"].as<JsonObject>();
      const char* kind = target["kind"] | "bundle";
      const char* value = target["value"] | "";
      t.app_kind = appTargetKindFromString(kind);
      strncpy(t.app_value, value, GRID_APP_VALUE_MAX);
    }
  }

  if (!gridConfigValidate(cfg, err, err_len)) {
    return false;
  }
  out = cfg;
  return true;
}

bool ProfileStore::load(GridConfig& out) {
  if (!ready_ || !LittleFS.exists(GRID_FILE_PATH)) {
    return false;
  }
  File f = LittleFS.open(GRID_FILE_PATH, "r");
  if (!f) {
    return false;
  }
  String body = f.readString();
  f.close();
  char err[64];
  if (!parse(body.c_str(), body.length(), out, err, sizeof(err))) {
    Serial.printf("[FS] grid parse failed: %s\n", err);
    return false;
  }
  current_ = out;
  return true;
}

bool ProfileStore::save(const GridConfig& cfg) {
  char err[64];
  if (!gridConfigValidate(cfg, err, sizeof(err))) {
    Serial.printf("[FS] save reject: %s\n", err);
    return false;
  }
  String json;
  if (!serialize(cfg, json)) {
    Serial.println("[FS] serialize failed");
    return false;
  }

  File tmp = LittleFS.open(GRID_TMP_PATH, "w");
  if (!tmp) {
    Serial.println("[FS] open tmp failed");
    return false;
  }
  const size_t written = tmp.print(json);
  tmp.close();
  if (written != json.length()) {
    LittleFS.remove(GRID_TMP_PATH);
    return false;
  }
  LittleFS.remove(GRID_FILE_PATH);
  if (!LittleFS.rename(GRID_TMP_PATH, GRID_FILE_PATH)) {
    Serial.println("[FS] rename failed");
    return false;
  }
  current_ = cfg;
  Serial.printf("[FS] Grid saved rev=%u (%u bytes)\n", cfg.rev, static_cast<unsigned>(json.length()));
  return true;
}

bool ProfileStore::resetToDefault(GridConfig& out) {
  gridConfigSetDefaults(out);
  if (!save(out)) {
    current_ = out;
    return false;
  }
  return true;
}
