#pragma once

#include <Arduino.h>

#include "app/input_queue.h"

bool eventServerBegin(uint16_t port = 81);
void eventServerTick();
void eventServerBroadcastTilePress(const InputEvent& ev);
// Tells the companion a media tile was pressed. `handled` is true when the board
// already delivered a USB/BLE HID key, so the companion must not repeat it.
void eventServerBroadcastMediaPress(const InputEvent& ev, bool handled);
void eventServerBroadcast(const String& json);
uint8_t eventServerClientCount();
bool eventServerIsRunning();
