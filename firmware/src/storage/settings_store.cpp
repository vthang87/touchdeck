#include "settings_store.h"

#include <Preferences.h>

#include "app_config.h"

SettingsStore settingsStore;

void SettingsStore::begin() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[NVS] Failed to open namespace");
    return;
  }
  prefs.end();
  Serial.println("[NVS] Settings store ready");
}

DeviceSettings SettingsStore::load() const {
  DeviceSettings s;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    s.device_name = APP_DEFAULT_DEVICE_NAME;
    s.ble_name = APP_DEFAULT_DEVICE_NAME;
    s.ota_password = APP_DEFAULT_OTA_PASSWORD;
    s.hostname = "touchdeck";
    s.idle_dim_s = APP_IDLE_DIM_S_DEFAULT;
    s.idle_clock_s = APP_IDLE_CLOCK_S_DEFAULT;
    s.idle_dim2_s = APP_IDLE_DIM2_S_DEFAULT;
    s.idle_off_s = APP_IDLE_OFF_S_DEFAULT;
    s.idle_dim_pct = APP_BRIGHTNESS_DIM_DEFAULT;
    s.idle_dim2_pct = APP_BRIGHTNESS_DIM2_DEFAULT;
    s.clock_font_px = APP_CLOCK_FONT_PX_DEFAULT;
    return s;
  }

  s.wifi_ssid = prefs.getString("wifi_ssid", "");
  s.wifi_password = prefs.getString("wifi_password", "");
  s.device_name = prefs.getString("device_name", APP_DEFAULT_DEVICE_NAME);
  s.ble_name = prefs.getString("ble_name", APP_DEFAULT_DEVICE_NAME);
  s.ota_password = prefs.getString("ota_password", APP_DEFAULT_OTA_PASSWORD);
  s.hostname = prefs.getString("hostname", "touchdeck");
  s.provisioned = prefs.getBool("provisioned", false);
  s.ble_enabled = prefs.getBool("ble_enabled", true);
  s.ble_pair_mode = prefs.getBool("ble_pair", true);
  s.idle_dim_s = prefs.getUShort("idle_dim", APP_IDLE_DIM_S_DEFAULT);
  s.idle_clock_s = prefs.getUShort("idle_clock", APP_IDLE_CLOCK_S_DEFAULT);
  s.idle_dim2_s = prefs.getUShort("idle_dim2", APP_IDLE_DIM2_S_DEFAULT);
  s.idle_off_s = prefs.getUShort("idle_off", APP_IDLE_OFF_S_DEFAULT);
  s.idle_dim_pct = prefs.getUChar("dim_pct", APP_BRIGHTNESS_DIM_DEFAULT);
  s.idle_dim2_pct = prefs.getUChar("dim2_pct", APP_BRIGHTNESS_DIM2_DEFAULT);
  s.clock_font_px = prefs.getUChar("clk_font", APP_CLOCK_FONT_PX_DEFAULT);
  prefs.end();

  Serial.println("[NVS] Settings loaded");
  return s;
}

bool SettingsStore::save(const DeviceSettings& settings) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.putString("wifi_ssid", settings.wifi_ssid);
  prefs.putString("wifi_password", settings.wifi_password);
  prefs.putString("device_name", settings.device_name);
  prefs.putString("ble_name", settings.ble_name);
  prefs.putString("ota_password", settings.ota_password);
  prefs.putString("hostname", settings.hostname);
  prefs.putBool("provisioned", settings.provisioned);
  prefs.putBool("ble_enabled", settings.ble_enabled);
  prefs.putBool("ble_pair", settings.ble_pair_mode);
  prefs.putUShort("idle_dim", settings.idle_dim_s);
  prefs.putUShort("idle_clock", settings.idle_clock_s);
  prefs.putUShort("idle_dim2", settings.idle_dim2_s);
  prefs.putUShort("idle_off", settings.idle_off_s);
  prefs.putUChar("dim_pct", settings.idle_dim_pct);
  prefs.putUChar("dim2_pct", settings.idle_dim2_pct);
  prefs.putUChar("clk_font", settings.clock_font_px);
  prefs.end();
  Serial.println("[NVS] Settings saved");
  return true;
}

bool SettingsStore::clearNetwork() {
  DeviceSettings s = load();
  s.wifi_ssid = "";
  s.wifi_password = "";
  s.provisioned = false;
  return save(s);
}

uint8_t SettingsStore::bumpBootAttempts() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return 0;
  }
  const uint8_t next = prefs.getUChar("boot_try", 0) + 1;
  prefs.putUChar("boot_try", next);
  prefs.end();
  return next;
}

void SettingsStore::clearBootAttempts() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  if (prefs.getUChar("boot_try", 0) != 0) {
    prefs.putUChar("boot_try", 0);
  }
  prefs.end();
}

bool SettingsStore::factoryReset() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.clear();
  prefs.end();
  Serial.println("[NVS] Factory reset");
  return true;
}
