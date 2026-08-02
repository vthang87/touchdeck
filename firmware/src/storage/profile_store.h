#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "grid_config.h"

class ProfileStore {
 public:
  bool begin();
  bool load(DeckProfile& out);
  bool save(const DeckProfile& profile);
  bool resetToDefault(DeckProfile& out);
  bool serialize(const DeckProfile& profile, String& out_json) const;
  bool parse(const char* json, size_t len, DeckProfile& out, char* err, size_t err_len) const;

  /** Legacy helpers used by older call sites — operate on shortcut page 0. */
  bool load(GridConfig& out);
  bool save(const GridConfig& cfg);
  bool resetToDefault(GridConfig& out);
  bool serialize(const GridConfig& cfg, String& out_json) const;
  bool parse(const char* json, size_t len, GridConfig& out, char* err, size_t err_len) const;

  const DeckProfile& profile() const { return profile_; }
  const GridConfig& current() const { return profile_.pages[0]; }
  bool ready() const { return ready_; }

 private:
  bool parseGridObject(JsonObject obj, GridConfig& out, char* err, size_t err_len) const;
  void serializeGridObject(JsonObject obj, const GridConfig& cfg) const;

  bool ready_ = false;
  DeckProfile profile_{};
};

extern ProfileStore profileStore;
