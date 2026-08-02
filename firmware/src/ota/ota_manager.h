#pragma once

#include <Arduino.h>

#include "storage/settings_store.h"

class OtaManager {
 public:
  void begin(const DeviceSettings& settings);
  void tick();
  bool isReady() const { return ready_; }

 private:
  bool ready_ = false;
  String password_;
};

extern OtaManager otaManager;
