#pragma once

#include <stdint.h>

void notificationOverlayBegin();
void notificationOverlaySet(const char* id, const char* source, const char* title, const char* body);
void notificationOverlayClear(const char* id);
void notificationOverlayClearAll();
void notificationOverlayTick();
uint8_t notificationOverlayCount();
bool notificationOverlayIsPending(const char* source);
