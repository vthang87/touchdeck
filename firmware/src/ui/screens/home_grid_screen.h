#pragma once

#include <lvgl.h>

#include "grid_config.h"

bool homeGridScreenCreate();
bool homeGridScreenCreateOn(lv_obj_t* parent);
/** Mount a shortcut page into `parent` (index 0 .. DECK_SHORTCUT_PAGES_MAX-1). */
bool homeGridScreenCreateAt(lv_obj_t* parent, uint8_t shortcut_index);
bool homeGridScreenReload(const GridConfig& cfg);
/** Caller already holds the LVGL mutex (e.g. inside an LVGL event). */
bool homeGridScreenReloadLocked(const GridConfig& cfg);
bool homeGridScreenReloadPageLocked(uint8_t shortcut_index, const GridConfig& cfg);
void homeGridScreenTick();
void homeGridScreenSetVolume(int volume, bool muted);
int homeGridScreenGetVolume();
bool homeGridScreenIsMuted();
void homeGridScreenSetApprovalHighlight(const char* source, bool on);
void homeGridScreenClearApprovalHighlights();
lv_obj_t* homeGridScreenRoot();
