#include "ota_manager.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "app_config.h"
#include "system/system_status.h"

OtaManager otaManager;

void OtaManager::begin(const DeviceSettings& settings) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Skip — Wi-Fi not connected");
    systemStatus.setOta(SystemOtaState::Idle);
    ready_ = false;
    return;
  }

  password_ = settings.ota_password.length() ? settings.ota_password : APP_DEFAULT_OTA_PASSWORD;
  ArduinoOTA.setHostname(settings.hostname.length() ? settings.hostname.c_str() : "touchdeck");
  ArduinoOTA.setPassword(password_.c_str());

  ArduinoOTA.onStart([]() {
    systemStatus.setOta(SystemOtaState::Starting);
    Serial.println("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    systemStatus.setOta(SystemOtaState::Success);
    Serial.println("\n[OTA] Success");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    systemStatus.setOta(SystemOtaState::Updating);
    static int last = -1;
    const int pct = total ? static_cast<int>(progress * 100 / total) : 0;
    if (pct != last && pct % 10 == 0) {
      last = pct;
      Serial.printf("[OTA] Progress %d%%\n", pct);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    systemStatus.setOta(SystemOtaState::Failed);
    Serial.printf("[OTA] Failed error=%u\n", error);
  });

  ArduinoOTA.begin();
  ready_ = true;
  systemStatus.setOta(SystemOtaState::Ready);
  Serial.printf("[OTA] Ready on %s.local\n", settings.hostname.c_str());
}

void OtaManager::tick() {
  if (ready_) {
    ArduinoOTA.handle();
  }
}
