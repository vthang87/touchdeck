#include "ble_hid.h"

bool bleHidBegin(const String& /*device_name*/, bool /*pair_mode*/) {
  Serial.println("[BLE] HID disabled (protocol v4 — companion owns media)");
  return false;
}

void bleHidTick() {}

bool bleHidIsConnected() { return false; }

bool bleHidIsStarted() { return false; }

bool bleHidPairMode() { return false; }

void bleHidSetPairMode(bool /*enabled*/) {}

void bleHidSend(InputAction /*action*/) {}

String bleHidDeviceName() { return String(); }
