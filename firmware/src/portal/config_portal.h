#pragma once

#include <Arduino.h>

#include "storage/settings_store.h"

void configPortalBegin(const DeviceSettings& current);
void configPortalTick();
void configPortalStop();
bool configPortalIsRunning();
void configPortalEnsureStarted(const DeviceSettings& current);
