#pragma once

#include <Arduino.h>

bool clockScreenCreate();
void clockScreenShow();
void clockScreenHide();
void clockScreenTick();
bool clockScreenIsVisible();
// Valid sizes: 48, 72, 96, 128, 160. Anything else falls back to 96.
void clockScreenSetFontSize(uint8_t px);
