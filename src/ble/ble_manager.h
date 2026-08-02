#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app/input_queue.h"

// Optional GATT companion surface (Status / Device Info / Command / Event)
bool bleManagerBegin();
void bleManagerTick();
void bleManagerNotifyStatus();
void bleManagerNotifyEvent(const String& json);
void bleManagerNotifyTilePress(const InputEvent& ev);
