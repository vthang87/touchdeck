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

  if (!load(profile_)) {
    Serial.println("[FS] No grid — writing deck defaults");
    resetToDefault(profile_);
  } else {
    Serial.printf("[FS] Deck loaded rev=%u pages=%u shortcuts=%u\n", profile_.rev, profile_.page_count,
                  profile_.shortcut_count);
  }
  return true;
}

void ProfileStore::serializeGridObject(JsonObject obj, const GridConfig& cfg) const {
  obj["cols"] = cfg.cols;
  obj["rows"] = cfg.rows;
  JsonArray tiles = obj["tiles"].to<JsonArray>();
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
    o["action_id"] = t.action_id;
    if (t.action == TileAction::App && t.app_value[0] != '\0') {
      JsonObject target = o["target"].to<JsonObject>();
      target["kind"] = appTargetKindToString(t.app_kind);
      target["value"] = t.app_value;
    }
  }
}

bool ProfileStore::parseGridObject(JsonObject obj, GridConfig& out, char* err, size_t err_len) const {
  auto fail = [&](const char* msg) -> bool {
    if (err && err_len) {
      strncpy(err, msg, err_len - 1);
      err[err_len - 1] = '\0';
    }
    return false;
  };

  memset(&out, 0, sizeof(out));
  out.rev = 1;
  out.cols = obj["cols"] | 0;
  out.rows = obj["rows"] | 0;
  JsonArray tiles = obj["tiles"].as<JsonArray>();
  if (tiles.isNull()) {
    return fail("tiles missing");
  }
  if (tiles.size() > GRID_MAX_TILES) {
    return fail("too many tiles");
  }
  out.tile_count = static_cast<uint8_t>(tiles.size());
  uint8_t i = 0;
  for (JsonObject o : tiles) {
    GridTile& t = out.tiles[i++];
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
    const char* action_id = o["action_id"] | "";
    strncpy(t.action_id, action_id, GRID_ACTION_ID_MAX);
    if (t.action == TileAction::App) {
      JsonObject target = o["target"].as<JsonObject>();
      const char* kind = target["kind"] | "bundle";
      const char* value = target["value"] | "";
      t.app_kind = appTargetKindFromString(kind);
      strncpy(t.app_value, value, GRID_APP_VALUE_MAX);
    }
    gridConfigEnsureActionId(t);
  }

  if (!gridConfigValidate(out, err, err_len)) {
    return false;
  }
  return true;
}

bool ProfileStore::serialize(const DeckProfile& profile, String& out_json) const {
  JsonDocument doc;
  doc["rev"] = profile.rev;
  doc["page_count"] = profile.page_count;
  JsonArray pages = doc["pages"].to<JsonArray>();
  for (uint8_t i = 0; i < profile.shortcut_count; ++i) {
    JsonObject page = pages.add<JsonObject>();
    serializeGridObject(page, profile.pages[i]);
  }
  // Also emit legacy flat fields from page 0 for older tools.
  doc["cols"] = profile.pages[0].cols;
  doc["rows"] = profile.pages[0].rows;
  JsonArray tiles = doc["tiles"].to<JsonArray>();
  for (uint8_t i = 0; i < profile.pages[0].tile_count; ++i) {
    const GridTile& t = profile.pages[0].tiles[i];
    JsonObject o = tiles.add<JsonObject>();
    o["id"] = t.id;
    o["label"] = t.label;
    char color_hex[10];
    snprintf(color_hex, sizeof(color_hex), "#%06X", static_cast<unsigned>(t.color & 0xFFFFFF));
    o["color"] = color_hex;
    o["icon"] = t.icon;
    o["action"] = tileActionToString(t.action);
    o["action_id"] = t.action_id;
    if (t.action == TileAction::App && t.app_value[0] != '\0') {
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

bool ProfileStore::parse(const char* json, size_t len, DeckProfile& out, char* err, size_t err_len) const {
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
  if (deserializeJson(doc, json, len)) {
    return fail("invalid json");
  }

  memset(&out, 0, sizeof(out));
  out.rev = doc["rev"] | 1;

  JsonArray pages = doc["pages"].as<JsonArray>();
  if (!pages.isNull() && pages.size() > 0) {
    out.page_count = doc["page_count"] | static_cast<int>(pages.size() + 1);
    if (out.page_count < DECK_PAGE_MIN) out.page_count = DECK_PAGE_MIN;
    if (out.page_count > DECK_PAGE_MAX) out.page_count = DECK_PAGE_MAX;
    out.shortcut_count = 0;
    for (JsonObject page : pages) {
      if (out.shortcut_count >= DECK_SHORTCUT_PAGES_MAX) break;
      if (!parseGridObject(page, out.pages[out.shortcut_count], err, err_len)) {
        return false;
      }
      out.pages[out.shortcut_count].rev = out.rev;
      out.shortcut_count++;
    }
    // Align page_count with actual shortcuts if needed.
    const uint8_t want = static_cast<uint8_t>(out.page_count - 1);
    while (out.shortcut_count < want && out.shortcut_count < DECK_SHORTCUT_PAGES_MAX) {
      gridConfigSetDefaults(out.pages[out.shortcut_count]);
      out.pages[out.shortcut_count].rev = out.rev;
      out.shortcut_count++;
    }
    if (out.shortcut_count > want) {
      out.shortcut_count = want;
    }
  } else {
    // Legacy flat grid → one shortcut page.
    GridConfig legacy;
    if (!parse(json, len, legacy, err, err_len)) {
      return false;
    }
    out.rev = legacy.rev;
    out.page_count = DECK_PAGE_MIN;
    out.shortcut_count = 1;
    out.pages[0] = legacy;
  }

  deckProfileEnsureShortcutCount(out);
  if (!deckProfileValidate(out, err, err_len)) {
    return false;
  }
  return true;
}

bool ProfileStore::load(DeckProfile& out) {
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
    Serial.printf("[FS] deck parse failed: %s\n", err);
    return false;
  }
  profile_ = out;
  return true;
}

bool ProfileStore::save(const DeckProfile& profile) {
  char err[64];
  // Keep the working copy off the caller's stack (~9KB DeckProfile).
  static DeckProfile copy;
  copy = profile;
  deckProfileEnsureShortcutCount(copy);
  if (!deckProfileValidate(copy, err, sizeof(err))) {
    Serial.printf("[FS] save reject: %s\n", err);
    return false;
  }
  String json;
  if (!serialize(copy, json)) {
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
  profile_ = copy;
  Serial.printf("[FS] Deck saved rev=%u pages=%u (%u bytes)\n", copy.rev, copy.page_count,
                static_cast<unsigned>(json.length()));
  return true;
}

bool ProfileStore::resetToDefault(DeckProfile& out) {
  deckProfileSetDefaults(out);
  if (!save(out)) {
    profile_ = out;
    return false;
  }
  return true;
}

// --- Legacy GridConfig wrappers ---

bool ProfileStore::serialize(const GridConfig& cfg, String& out_json) const {
  static DeckProfile p;
  p = profile_;
  p.pages[0] = cfg;
  p.pages[0].rev = cfg.rev;
  p.rev = cfg.rev;
  if (p.page_count < DECK_PAGE_MIN) {
    p.page_count = DECK_PAGE_MIN;
  }
  deckProfileEnsureShortcutCount(p);
  return serialize(p, out_json);
}

bool ProfileStore::parse(const char* json, size_t len, GridConfig& out, char* err, size_t err_len) const {
  // Parse as flat legacy object only (used during migration path).
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
  if (deserializeJson(doc, json, len)) {
    return fail("invalid json");
  }
  return parseGridObject(doc.as<JsonObject>(), out, err, err_len);
}

bool ProfileStore::load(GridConfig& out) {
  static DeckProfile p;
  if (!load(p)) {
    return false;
  }
  out = p.pages[0];
  return true;
}

bool ProfileStore::save(const GridConfig& cfg) {
  static DeckProfile p;
  p = profile_;
  if (p.page_count < DECK_PAGE_MIN) {
    deckProfileSetDefaults(p);
  }
  p.rev = cfg.rev;
  p.pages[0] = cfg;
  p.pages[0].rev = cfg.rev;
  deckProfileEnsureShortcutCount(p);
  return save(p);
}

bool ProfileStore::resetToDefault(GridConfig& out) {
  DeckProfile p;
  if (!resetToDefault(p)) {
    out = p.pages[0];
    return false;
  }
  out = p.pages[0];
  return true;
}
