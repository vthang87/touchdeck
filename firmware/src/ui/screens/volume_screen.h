#pragma once

#include <Arduino.h>
#include <lvgl.h>

void volumeScreenCreate();
void volumeScreenSetLevel(int volume, bool muted);
int volumeScreenGetLevel();
bool volumeScreenIsMuted();
