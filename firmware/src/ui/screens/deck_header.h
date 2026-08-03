#pragma once

#include <lvgl.h>
#include <stdint.h>

/** Shared top menu bar (device name + clock/volume/BT/link/Wi-Fi). */
bool deckHeaderCreate(lv_obj_t* parent);
void deckHeaderTick();
void deckHeaderSetVolume(int volume, bool muted);
void deckHeaderRaise();
lv_coord_t deckHeaderHeight();
lv_obj_t* deckHeaderRoot();
