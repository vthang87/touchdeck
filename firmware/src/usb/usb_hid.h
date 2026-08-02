#pragma once

#include <Arduino.h>

#include "app/input_queue.h"

bool usbHidBegin(const String& product_name);
void usbHidTick();
bool usbHidIsReady();
void usbHidSend(InputAction action);
