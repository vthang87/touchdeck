#pragma once

#include <Arduino.h>

#include "storage/settings_store.h"

enum class IdleState : uint8_t {
  Active = 0,
  Dim,
  Clock,
  Dim2,
  Off,
};

void idleManagerBegin(const DeviceSettings& settings);
void idleManagerApplySettings(const DeviceSettings& settings);
void idleManagerTick();
// Call on any user activity (touch). Returns true if this touch woke from clock/off.
bool idleManagerNoteActivity();
// Call when the finger lifts — clears wake-suppress so the next tap can fire tiles.
void idleManagerNoteRelease();
IdleState idleManagerState();
// Home tiles should only fire while Active/Dim and not on the wake press.
bool idleManagerAllowTilePress();
// Freeze state transitions so brightness can be probed by hand.
void idleManagerSetHold(bool hold);
bool idleManagerHold();
// Wake the display when a remote alert arrives (no touch required).
void idleManagerWakeForAlert();
