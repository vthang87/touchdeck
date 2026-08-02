#pragma once

#include <Arduino.h>

#include "app/input_queue.h"

bool bleHidBegin(const String& device_name, bool pair_mode = true);
void bleHidTick();
bool bleHidIsConnected();
bool bleHidIsStarted();
bool bleHidPairMode();
// Discoverable advertising for new pairing / reconnect.
void bleHidSetPairMode(bool enabled);
void bleHidSend(InputAction action);
String bleHidDeviceName();
