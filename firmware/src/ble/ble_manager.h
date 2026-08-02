#pragma once

#include <Arduino.h>

#include "app/input_queue.h"

// GATT-only companion surface (no BLE HID). Owns NimBLE init + advertising.
bool bleManagerBegin(const String& device_name, bool pair_mode = true);
void bleManagerTick();
bool bleManagerIsConnected();
bool bleManagerIsStarted();
bool bleManagerPairMode();
void bleManagerSetPairMode(bool enabled);
String bleManagerDeviceName();

void bleManagerNotifyStatus();
void bleManagerNotifyEvent(const String& json);
void bleManagerNotifyTilePress(const InputEvent& ev);
