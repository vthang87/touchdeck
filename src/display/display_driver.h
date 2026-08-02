#pragma once

#include <Arduino.h>
#include <lvgl.h>

bool displayDriverBegin();
void displayDriverStartTask();
void displayDriverSetBacklight(uint8_t percent);
uint8_t displayDriverGetBacklight();
void displayDriverSetBacklightFreq(uint32_t hz);
uint32_t displayDriverGetBacklightFreq();
void displayDriverTick();
bool displayDriverLock(uint32_t timeout_ms);
void displayDriverUnlock();
