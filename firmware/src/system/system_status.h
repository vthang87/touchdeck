#pragma once

#include <Arduino.h>

enum class SystemWifiState : uint8_t {
  Idle,
  Connecting,
  Connected,
  Disconnected,
  ApMode,
  Error,
};

enum class SystemBleState : uint8_t {
  Stopped,
  Advertising,
  Connected,
  Disconnected,
  Error,
};

enum class SystemOtaState : uint8_t {
  Idle,
  Ready,
  Starting,
  Updating,
  Success,
  Failed,
};

struct SystemSnapshot {
  SystemWifiState wifi = SystemWifiState::Idle;
  SystemBleState ble = SystemBleState::Stopped;
  SystemOtaState ota = SystemOtaState::Idle;
  String ip;
  String hostname;
  uint32_t uptime_sec = 0;
  uint32_t free_heap = 0;
  int volume = 50;
  bool muted = false;
};

class SystemStatus {
 public:
  void begin();
  void setWifi(SystemWifiState state, const String& ip = String());
  void setBle(SystemBleState state);
  void setOta(SystemOtaState state);
  void setHostname(const String& hostname);
  void setVolume(int volume, bool muted);
  void tick();
  SystemSnapshot snapshot() const;

 private:
  SystemSnapshot snap_;
};

extern SystemStatus systemStatus;

const char* wifiStateName(SystemWifiState state);
const char* bleStateName(SystemBleState state);
const char* otaStateName(SystemOtaState state);
