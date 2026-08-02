#pragma once

#include <Arduino.h>

class AppManager {
 public:
  void begin();
  void loop();

 private:
  void processInput();
  void maybeStartOta();

  bool ota_started_ = false;
  bool gatt_started_ = false;
  bool boot_marked_stable_ = false;
  uint32_t last_status_ms_ = 0;
};

extern AppManager appManager;
