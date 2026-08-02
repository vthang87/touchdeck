#pragma once

#include <Arduino.h>

struct DeviceSettings {
  String wifi_ssid;
  String wifi_password;
  String device_name;
  String ble_name;
  String ota_password;
  String hostname;
  bool provisioned = false;
  bool ble_enabled = true;
  bool ble_pair_mode = true;

  // Idle timeouts in seconds. 0 disables that stage.
  uint16_t idle_dim_s = 30;
  uint16_t idle_clock_s = 120;
  uint16_t idle_dim2_s = 300;
  uint16_t idle_off_s = 1800;
  uint8_t idle_dim_pct = 30;
  uint8_t idle_dim2_pct = 30;

  // Clock face height in px. Only the sizes with a bundled font are valid.
  uint8_t clock_font_px = 96;
};

class SettingsStore {
 public:
  void begin();
  DeviceSettings load() const;
  bool save(const DeviceSettings& settings);
  bool clearNetwork();
  bool factoryReset();

  // Boot-loop guard: a bad network config must not brick the device.
  uint8_t bumpBootAttempts();
  void clearBootAttempts();

 private:
  static constexpr const char* kNamespace = "touchdeck";
};

extern SettingsStore settingsStore;
