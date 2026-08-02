#include "wifi_manager.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

#include "app_config.h"
#include "portal/config_portal.h"

WifiManager wifiManager;

String WifiManager::macSuffix() const {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String WifiManager::makeApSsid() const {
  return String("TouchDeck-Setup-") + macSuffix();
}

void WifiManager::begin(const DeviceSettings& settings) {
  settings_ = settings;
  hostname_ = settings.hostname;
  if (hostname_.isEmpty()) {
    hostname_ = String("touchdeck-") + macSuffix();
  }
  hostname_.toLowerCase();
  systemStatus.setHostname(hostname_);
  WiFi.setHostname(hostname_.c_str());

  if (settings_.provisioned && settings_.wifi_ssid.length() > 0) {
    tryConnect();
  } else {
    enterApMode();
  }
}

void WifiManager::tryConnect() {
  phase_ = Phase::Connecting;
  phase_since_ms_ = millis();
  retries_ = 0;
  systemStatus.setWifi(SystemWifiState::Connecting);
  Serial.printf("[WIFI] Connecting to %s\n", settings_.wifi_ssid.c_str());
  WiFi.mode(WIFI_STA);
  // ESP-IDF aborts if STA runs without modem sleep while Bluetooth is enabled.
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.begin(settings_.wifi_ssid.c_str(), settings_.wifi_password.c_str());
}

void WifiManager::enterApMode() {
  phase_ = Phase::ApMode;
  phase_since_ms_ = millis();
  next_sta_attempt_ms_ = millis() + APP_AP_RECHECK_INTERVAL_MS;
  const String ssid = makeApSsid();
  WiFi.mode(WIFI_AP);
  // Channel 1 and an explicit client limit: some hosts fail to join the default config.
  const bool ap_ok = WiFi.softAP(ssid.c_str(), APP_DEFAULT_AP_PASSWORD, 1, 0, 4);
  systemStatus.setWifi(SystemWifiState::ApMode, WiFi.softAPIP().toString());
  Serial.printf("[WIFI] AP %s IP %s (softAP %s)\n", ssid.c_str(), WiFi.softAPIP().toString().c_str(),
                ap_ok ? "ok" : "FAILED");
  Serial.printf("[WIFI] AP password \"%s\" (%u chars, channel 1)\n", APP_DEFAULT_AP_PASSWORD,
                static_cast<unsigned>(strlen(APP_DEFAULT_AP_PASSWORD)));
  configPortalBegin(settings_);
}

void WifiManager::startConfigPortal() { enterApMode(); }

void WifiManager::tick() {
  const uint32_t now = millis();

  if (phase_ == Phase::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      phase_ = Phase::Connected;
      const String ip = WiFi.localIP().toString();
      systemStatus.setWifi(SystemWifiState::Connected, ip);
      Serial.printf("[WIFI] Connected IP %s hostname %s.local\n", ip.c_str(), hostname_.c_str());
      if (MDNS.begin(hostname_.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("touchdeck", "tcp", 81);
        Serial.println("[WIFI] mDNS started");
      }
      configTime(0, 0, APP_NTP_SERVER);
      setenv("TZ", APP_TIMEZONE_POSIX, 1);
      tzset();
      Serial.printf("[TIME] NTP sync started (%s, TZ %s)\n", APP_NTP_SERVER, APP_TIMEZONE_POSIX);
      configPortalEnsureStarted(settings_);
      return;
    }
    if (now - phase_since_ms_ > APP_WIFI_CONNECT_TIMEOUT_MS) {
      ++retries_;
      WiFi.disconnect(true);
      if (retries_ >= APP_WIFI_MAX_RETRIES) {
        Serial.println("[WIFI] Connect failed — opening AP");
        enterApMode();
      } else {
        phase_ = Phase::RetryWait;
        phase_since_ms_ = now;
        systemStatus.setWifi(SystemWifiState::Disconnected);
        Serial.printf("[WIFI] Retry %u/%u\n", retries_, APP_WIFI_MAX_RETRIES);
      }
    }
    return;
  }

  if (phase_ == Phase::RetryWait) {
    if (now - phase_since_ms_ >= APP_WIFI_RETRY_DELAY_MS) {
      phase_ = Phase::Connecting;
      phase_since_ms_ = now;
      systemStatus.setWifi(SystemWifiState::Connecting);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(WIFI_PS_MIN_MODEM);
      WiFi.begin(settings_.wifi_ssid.c_str(), settings_.wifi_password.c_str());
    }
    return;
  }

  if (phase_ == Phase::Connected) {
    configPortalTick();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Lost connection — reconnecting");
      systemStatus.setWifi(SystemWifiState::Disconnected);
      tryConnect();
    }
    return;
  }

  if (phase_ == Phase::ApMode) {
    configPortalTick();
    if (settings_.provisioned && settings_.wifi_ssid.length() > 0 && now >= next_sta_attempt_ms_) {
      next_sta_attempt_ms_ = now + APP_AP_RECHECK_INTERVAL_MS;
      // Non-blocking STA probe — never stall the UI loop for seconds.
      WiFi.mode(WIFI_AP_STA);
      WiFi.setSleep(WIFI_PS_MIN_MODEM);
      WiFi.begin(settings_.wifi_ssid.c_str(), settings_.wifi_password.c_str());
      phase_ = Phase::Connecting;
      phase_since_ms_ = now;
      retries_ = 0;
      systemStatus.setWifi(SystemWifiState::Connecting);
      Serial.println("[WIFI] AP mode probing STA (non-blocking)");
    }
  }
}

bool WifiManager::isConnected() const { return phase_ == Phase::Connected && WiFi.status() == WL_CONNECTED; }

bool WifiManager::isApMode() const { return phase_ == Phase::ApMode; }

String WifiManager::ipAddress() const {
  if (phase_ == Phase::ApMode) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

String WifiManager::hostname() const { return hostname_; }
