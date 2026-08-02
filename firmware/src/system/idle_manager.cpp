#include "idle_manager.h"

#include "app_config.h"
#include "display/display_driver.h"
#include "ui/screens/clock_screen.h"

namespace {

IdleState s_state = IdleState::Active;
uint32_t s_last_activity_ms = 0;
uint16_t s_dim_s = APP_IDLE_DIM_S_DEFAULT;
uint16_t s_clock_s = APP_IDLE_CLOCK_S_DEFAULT;
uint16_t s_dim2_s = APP_IDLE_DIM2_S_DEFAULT;
uint16_t s_off_s = APP_IDLE_OFF_S_DEFAULT;
uint8_t s_dim_pct = APP_BRIGHTNESS_DIM_DEFAULT;
uint8_t s_dim2_pct = APP_BRIGHTNESS_DIM2_DEFAULT;
volatile bool s_pending_active = false;
bool s_hold = false;
// Swallow the finger-down that woke the screen so it cannot fire a tile
// after the clock → home swap (finger is still down when the grid appears).
bool s_wake_suppress = false;

const char* stateName(IdleState state) {
  switch (state) {
    case IdleState::Active: return "active";
    case IdleState::Dim: return "dim";
    case IdleState::Clock: return "clock";
    case IdleState::Dim2: return "dim2";
    case IdleState::Off: return "off";
  }
  return "?";
}

void enterState(IdleState next) {
  if (next == s_state) {
    return;
  }
  const IdleState prev = s_state;
  s_state = next;
  Serial.printf("[IDLE] %s -> %s\n", stateName(prev), stateName(next));

  switch (next) {
    case IdleState::Active:
      displayDriverSetBacklight(APP_BRIGHTNESS_FULL);
      clockScreenHide();
      break;
    case IdleState::Dim:
      displayDriverSetBacklight(s_dim_pct);
      clockScreenHide();
      break;
    case IdleState::Clock:
      displayDriverSetBacklight(s_dim_pct);
      clockScreenShow();
      break;
    case IdleState::Dim2:
      displayDriverSetBacklight(s_dim2_pct);
      clockScreenShow();
      break;
    case IdleState::Off:
      displayDriverSetBacklight(0);
      clockScreenShow();
      break;
  }
}

IdleState stateForIdleMs(uint32_t idle_ms) {
  if (s_off_s > 0 && idle_ms >= static_cast<uint32_t>(s_off_s) * 1000UL) {
    return IdleState::Off;
  }
  if (s_dim2_s > 0 && idle_ms >= static_cast<uint32_t>(s_dim2_s) * 1000UL) {
    return IdleState::Dim2;
  }
  if (s_clock_s > 0 && idle_ms >= static_cast<uint32_t>(s_clock_s) * 1000UL) {
    return IdleState::Clock;
  }
  if (s_dim_s > 0 && idle_ms >= static_cast<uint32_t>(s_dim_s) * 1000UL) {
    return IdleState::Dim;
  }
  return IdleState::Active;
}

}  // namespace

void idleManagerBegin(const DeviceSettings& settings) {
  idleManagerApplySettings(settings);
  s_last_activity_ms = millis();
  s_state = IdleState::Active;
  s_pending_active = false;
  s_wake_suppress = false;
  displayDriverSetBacklight(APP_BRIGHTNESS_FULL);
  Serial.printf("[IDLE] dim=%us/%u%% clock=%us dim2=%us/%u%% off=%us\n", s_dim_s, s_dim_pct,
                s_clock_s, s_dim2_s, s_dim2_pct, s_off_s);
}

void idleManagerApplySettings(const DeviceSettings& settings) {
  s_dim_s = settings.idle_dim_s;
  s_clock_s = settings.idle_clock_s;
  s_dim2_s = settings.idle_dim2_s;
  s_off_s = settings.idle_off_s;
  s_dim_pct = constrain(settings.idle_dim_pct, 1, 100);
  s_dim2_pct = constrain(settings.idle_dim2_pct, 1, 100);
}

void idleManagerTick() {
  if (s_hold) {
    return;
  }
  // Touch may request Active while LVGL already holds the display lock — apply here.
  if (s_pending_active) {
    s_pending_active = false;
    enterState(IdleState::Active);
  }

  const uint32_t idle_ms = millis() - s_last_activity_ms;
  const IdleState wanted = stateForIdleMs(idle_ms);
  if (wanted != s_state) {
    enterState(wanted);
  }
  if (s_state == IdleState::Clock || s_state == IdleState::Dim2 || s_state == IdleState::Off) {
    clockScreenTick();
  }
}

bool idleManagerNoteActivity() {
  if (s_hold) {
    return false;
  }
  // Called from the LVGL input callback (display lock already held). Avoid LVGL
  // screen swaps here — only adjust backlight / schedule Active for the tick.
  s_last_activity_ms = millis();

  if (s_state == IdleState::Active) {
    return false;
  }

  if (s_state == IdleState::Dim) {
    displayDriverSetBacklight(APP_BRIGHTNESS_FULL);
    s_state = IdleState::Active;
    // Dim still shows the grid — treat this press as intentional (no suppress).
    Serial.println("[IDLE] dim -> active");
    return false;
  }

  // Clock / Dim2 / Off: defer screen swap to idleManagerTick().
  // Suppress tile presses until the finger lifts — otherwise the same touch
  // lands on a home-grid tile after the screen swap.
  s_wake_suppress = true;
  s_pending_active = true;
  displayDriverSetBacklight(APP_BRIGHTNESS_FULL);
  Serial.println("[IDLE] wake suppress until release");
  return true;
}

void idleManagerNoteRelease() {
  if (s_wake_suppress) {
    s_wake_suppress = false;
    Serial.println("[IDLE] wake suppress cleared");
  }
}

IdleState idleManagerState() { return s_state; }

bool idleManagerAllowTilePress() {
  if (s_wake_suppress) {
    return false;
  }
  return s_state == IdleState::Active || s_state == IdleState::Dim;
}

void idleManagerSetHold(bool hold) {
  s_hold = hold;
  s_last_activity_ms = millis();
  Serial.printf("[IDLE] hold %s\n", hold ? "on" : "off");
}

bool idleManagerHold() { return s_hold; }

void idleManagerWakeForAlert() {
  if (s_hold) {
    return;
  }
  s_last_activity_ms = millis();
  if (s_state != IdleState::Active) {
    s_pending_active = true;
  }
  displayDriverSetBacklight(APP_BRIGHTNESS_FULL);
  Serial.println("[IDLE] wake for alert");
}
