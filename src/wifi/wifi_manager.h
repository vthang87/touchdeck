#pragma once

#include <Arduino.h>

#include "storage/settings_store.h"
#include "system/system_status.h"

class WifiManager {
 public:
  void begin(const DeviceSettings& settings);
  void tick();
  bool isConnected() const;
  bool isApMode() const;
  String ipAddress() const;
  String hostname() const;
  void startConfigPortal();

 private:
  enum class Phase : uint8_t { Idle, Connecting, Connected, ApMode, RetryWait };

  void tryConnect();
  void enterApMode();
  String makeApSsid() const;
  String macSuffix() const;

  DeviceSettings settings_;
  Phase phase_ = Phase::Idle;
  uint8_t retries_ = 0;
  uint32_t phase_since_ms_ = 0;
  uint32_t next_sta_attempt_ms_ = 0;
  String hostname_;
};

extern WifiManager wifiManager;
