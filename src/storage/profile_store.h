#pragma once

#include <Arduino.h>

#include "grid_config.h"

class ProfileStore {
 public:
  bool begin();
  bool load(GridConfig& out);
  bool save(const GridConfig& cfg);
  bool resetToDefault(GridConfig& out);
  bool serialize(const GridConfig& cfg, String& out_json) const;
  bool parse(const char* json, size_t len, GridConfig& out, char* err, size_t err_len) const;
  const GridConfig& current() const { return current_; }
  bool ready() const { return ready_; }

 private:
  bool ready_ = false;
  GridConfig current_{};
};

extern ProfileStore profileStore;
