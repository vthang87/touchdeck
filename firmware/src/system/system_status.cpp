#include "system_status.h"

SystemStatus systemStatus;

void SystemStatus::begin() {
  snap_.uptime_sec = 0;
  snap_.free_heap = ESP.getFreeHeap();
  Serial.printf("[BOOT] Free heap=%u PSRAM=%u\n", ESP.getFreeHeap(), ESP.getPsramSize());
}

void SystemStatus::setWifi(SystemWifiState state, const String& ip) {
  snap_.wifi = state;
  if (ip.length()) {
    snap_.ip = ip;
  }
}

void SystemStatus::setBle(SystemBleState state) { snap_.ble = state; }

void SystemStatus::setOta(SystemOtaState state) { snap_.ota = state; }

void SystemStatus::setHostname(const String& hostname) { snap_.hostname = hostname; }

void SystemStatus::setVolume(int volume, bool muted) {
  snap_.volume = volume;
  snap_.muted = muted;
}

void SystemStatus::tick() {
  snap_.uptime_sec = millis() / 1000;
  snap_.free_heap = ESP.getFreeHeap();
}

SystemSnapshot SystemStatus::snapshot() const { return snap_; }

const char* wifiStateName(SystemWifiState state) {
  switch (state) {
    case SystemWifiState::Idle: return "IDLE";
    case SystemWifiState::Connecting: return "CONNECTING";
    case SystemWifiState::Connected: return "CONNECTED";
    case SystemWifiState::Disconnected: return "DISCONNECTED";
    case SystemWifiState::ApMode: return "AP_MODE";
    case SystemWifiState::Error: return "ERROR";
  }
  return "?";
}

const char* bleStateName(SystemBleState state) {
  switch (state) {
    case SystemBleState::Stopped: return "STOPPED";
    case SystemBleState::Advertising: return "ADVERTISING";
    case SystemBleState::Connected: return "CONNECTED";
    case SystemBleState::Disconnected: return "DISCONNECTED";
    case SystemBleState::Error: return "ERROR";
  }
  return "?";
}

const char* otaStateName(SystemOtaState state) {
  switch (state) {
    case SystemOtaState::Idle: return "IDLE";
    case SystemOtaState::Ready: return "READY";
    case SystemOtaState::Starting: return "STARTING";
    case SystemOtaState::Updating: return "UPDATING";
    case SystemOtaState::Success: return "SUCCESS";
    case SystemOtaState::Failed: return "FAILED";
  }
  return "?";
}
