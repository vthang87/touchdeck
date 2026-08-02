#pragma once

#include <Arduino.h>

#include "app/input_queue.h"

// BLE HID removed in protocol v4 — stubs kept so stray includes still compile.
bool bleHidBegin(const String& device_name, bool pair_mode = true);
void bleHidTick();
bool bleHidIsConnected();
bool bleHidIsStarted();
bool bleHidPairMode();
void bleHidSetPairMode(bool enabled);
void bleHidSend(InputAction action);
String bleHidDeviceName();
