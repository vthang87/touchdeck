#include "usb_hid.h"

#include "board_config.h"

#if BOARD_USB_HID_ENABLED

#include "USB.h"
#include "USBHID.h"
#include "USBHIDConsumerControl.h"

#if CONFIG_TINYUSB_HID_ENABLED

namespace {

USBHID s_hid;
USBHIDConsumerControl s_consumer;
bool s_started = false;

}  // namespace

bool usbHidBegin(const String& product_name) {
  const char* name = product_name.length() ? product_name.c_str() : "TouchDeck";
  USB.manufacturerName("TouchDeck");
  USB.productName(name);
  USB.serialNumber("1");

  s_consumer.begin();
  USB.begin();
  s_started = true;
  Serial.printf("[USB] HID Consumer Control started (%s)\n", name);
  return true;
}

void usbHidTick() {}

bool usbHidIsReady() { return s_started && s_hid.ready(); }

void usbHidSend(InputAction action) {
  if (!s_started) {
    return;
  }
  if (!s_hid.ready()) {
    Serial.println("[USB] skip — host not ready");
    return;
  }

  uint16_t key = 0;
  switch (action) {
    case InputAction::VolumeUp:
      key = CONSUMER_CONTROL_VOLUME_INCREMENT;
      Serial.println("[USB] volume_up");
      break;
    case InputAction::VolumeDown:
      key = CONSUMER_CONTROL_VOLUME_DECREMENT;
      Serial.println("[USB] volume_down");
      break;
    case InputAction::MuteToggle:
      key = CONSUMER_CONTROL_MUTE;
      Serial.println("[USB] mute");
      break;
    case InputAction::PlayPause:
      key = CONSUMER_CONTROL_PLAY_PAUSE;
      Serial.println("[USB] play_pause");
      break;
    case InputAction::NextTrack:
      key = CONSUMER_CONTROL_SCAN_NEXT;
      Serial.println("[USB] next");
      break;
    case InputAction::PrevTrack:
      key = CONSUMER_CONTROL_SCAN_PREVIOUS;
      Serial.println("[USB] previous");
      break;
    default:
      return;
  }

  s_consumer.press(key);
  delay(15);
  s_consumer.release();
  delay(2);
}

#else

bool usbHidBegin(const String& /*product_name*/) {
  Serial.println("[USB] TinyUSB HID disabled in this build");
  return false;
}

void usbHidTick() {}

bool usbHidIsReady() { return false; }

void usbHidSend(InputAction /*action*/) {}

#endif

#else  // !BOARD_USB_HID_ENABLED

bool usbHidBegin(const String& /*product_name*/) {
  // ESP32-S3 USB D-/D+ are GPIO19/20 — same pins as GT911 I2C on JC8048W550C.
  Serial.println("[USB] HID disabled — GPIO19/20 reserved for GT911 touch");
  return false;
}

void usbHidTick() {}

bool usbHidIsReady() { return false; }

void usbHidSend(InputAction /*action*/) {}

#endif
