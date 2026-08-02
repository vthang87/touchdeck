#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "grid_config.h"

bool workspacePagerCreate();
void workspacePagerTick();
void workspacePagerSetPage(uint8_t index);
uint8_t workspacePagerPage();
uint8_t workspacePagerPageCount();
bool workspacePagerSetPageCount(uint8_t total_pages);
bool workspacePagerReloadShortcuts();
/** Refresh pager from profileStore (no save). Call after HTTP grid save. */
void workspacePagerSyncFromStore();
/** True briefly after a page swipe — ignore tile/button clicks. */
bool workspacePagerSwipeSuppress();
/**
 * Call from button/tile PRESSED / PRESSING / CLICKED handlers.
 * Tracks drag distance; horizontal drag becomes a page swipe and blocks the click.
 * Returns true when the click must be ignored.
 */
bool workspacePagerTouchGate(lv_event_t* e);
lv_obj_t* workspacePagerRoot();
