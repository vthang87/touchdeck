#pragma once

#include <Arduino.h>

bool uiManagerBegin();
void uiManagerTick();
bool uiManagerReloadGrid();
bool uiManagerSetPageCount(uint8_t count);
void uiManagerSetPage(uint8_t index);
