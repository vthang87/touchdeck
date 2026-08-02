#pragma once

#include <lvgl.h>

#include "grid_config.h"

bool homeGridScreenCreate();
bool homeGridScreenCreateOn(lv_obj_t* parent);
bool homeGridScreenReload(const GridConfig& cfg);
/** Caller already holds the LVGL mutex (e.g. inside an LVGL event). */
bool homeGridScreenReloadLocked(const GridConfig& cfg);
void homeGridScreenTick();
void homeGridScreenSetVolume(int volume, bool muted);
int homeGridScreenGetVolume();
bool homeGridScreenIsMuted();
void homeGridScreenSetApprovalHighlight(const char* source, bool on);
void homeGridScreenClearApprovalHighlights();
lv_obj_t* homeGridScreenRoot();
