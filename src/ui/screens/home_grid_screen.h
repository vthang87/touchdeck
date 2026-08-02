#pragma once

#include <lvgl.h>

#include "grid_config.h"

bool homeGridScreenCreate();
bool homeGridScreenReload(const GridConfig& cfg);
void homeGridScreenTick();
void homeGridScreenSetVolume(int volume, bool muted);
int homeGridScreenGetVolume();
bool homeGridScreenIsMuted();
void homeGridScreenSetApprovalHighlight(const char* source, bool on);
void homeGridScreenClearApprovalHighlights();
lv_obj_t* homeGridScreenRoot();
