#include "workspace_pager.h"

#include <Preferences.h>
#include <string.h>

#include "board_config.h"
#include "display/display_driver.h"
#include "home_grid_screen.h"
#include "media_screen.h"
#include "storage/profile_store.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_media_host = nullptr;
lv_obj_t* s_grid_host = nullptr;
lv_obj_t* s_dots[DECK_PAGE_MAX] = {};
lv_obj_t* s_dot_row = nullptr;
uint8_t s_page_count = DECK_PAGE_MIN;
uint8_t s_page = 0;
uint8_t s_shortcut_idx = 0;
int8_t s_loaded_shortcut = -1;
bool s_on_media = true;
bool s_page_dirty = false;
uint32_t s_save_at_ms = 0;
uint32_t s_swipe_suppress_until_ms = 0;
lv_point_t s_press_pt = {0, 0};
bool s_press_tracking = false;
bool s_drag_is_swipe = false;
bool s_swipe_page_applied = false;

constexpr lv_coord_t kSwipeMinDx = 48;
constexpr lv_coord_t kClickMaxSlop = 18;

Preferences s_prefs;

void savePageNow() {
  s_prefs.begin("deck", false);
  s_prefs.putUChar("page", s_page);
  s_prefs.end();
  s_page_dirty = false;
}

void scheduleSavePage() {
  s_page_dirty = true;
  s_save_at_ms = millis() + 400;
}

uint8_t loadSavedPage() {
  s_prefs.begin("deck", true);
  const uint8_t p = s_prefs.getUChar("page", 0);
  s_prefs.end();
  return p;
}

void refreshDots() {
  for (uint8_t i = 0; i < DECK_PAGE_MAX; ++i) {
    if (!s_dots[i]) continue;
    if (i >= s_page_count) {
      lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    const bool on = (i == s_page);
    lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(on ? 0x38BDF8 : 0x334155), 0);
    lv_obj_set_style_bg_opa(s_dots[i], on ? LV_OPA_COVER : LV_OPA_60, 0);
  }
}

bool showShortcutLocked(uint8_t shortcut_index) {
  const DeckProfile& profile = profileStore.profile();
  if (shortcut_index >= profile.shortcut_count) {
    return false;
  }
  s_shortcut_idx = shortcut_index;
  if (s_loaded_shortcut == static_cast<int8_t>(shortcut_index)) {
    return true;
  }
  if (!homeGridScreenReloadLocked(profile.pages[shortcut_index])) {
    return false;
  }
  s_loaded_shortcut = static_cast<int8_t>(shortcut_index);
  return true;
}

void showHostsForPage(uint8_t page) {
  // Instant show/hide — no dual-page scroll redraw (was the swipe jank source).
  if (page == 0) {
    if (s_media_host) lv_obj_clear_flag(s_media_host, LV_OBJ_FLAG_HIDDEN);
    if (s_grid_host) lv_obj_add_flag(s_grid_host, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (s_grid_host) lv_obj_clear_flag(s_grid_host, LV_OBJ_FLAG_HIDDEN);
    if (s_media_host) lv_obj_add_flag(s_media_host, LV_OBJ_FLAG_HIDDEN);
  }
}

void applyPage(uint8_t page, bool /*animate*/) {
  if (page >= s_page_count) {
    page = static_cast<uint8_t>(s_page_count - 1);
  }
  s_page = page;
  if (page == 0) {
    s_on_media = true;
    showHostsForPage(0);
  } else {
    s_on_media = false;
    showShortcutLocked(static_cast<uint8_t>(page - 1));
    showHostsForPage(1);
  }
  refreshDots();
  scheduleSavePage();
}

void onDot(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  applyPage(static_cast<uint8_t>(idx), false);
}

void markSwipe(lv_indev_t* indev) {
  s_drag_is_swipe = true;
  s_swipe_suppress_until_ms = millis() + 600;
  if (indev) {
    lv_indev_wait_release(indev);
  }
}

void tryApplySwipeFromDx(lv_coord_t dx, lv_indev_t* indev) {
  if (s_swipe_page_applied) {
    return;
  }
  if (dx <= -kSwipeMinDx) {
    if (s_page + 1 < s_page_count) {
      applyPage(static_cast<uint8_t>(s_page + 1), false);
      s_swipe_page_applied = true;
    }
  } else if (dx >= kSwipeMinDx) {
    if (s_page > 0) {
      applyPage(static_cast<uint8_t>(s_page - 1), false);
      s_swipe_page_applied = true;
    }
  }
  if (s_swipe_page_applied) {
    markSwipe(indev);
  }
}

void onGesture(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev) return;
  const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT) {
    tryApplySwipeFromDx(-kSwipeMinDx, indev);
  } else if (dir == LV_DIR_RIGHT) {
    tryApplySwipeFromDx(kSwipeMinDx, indev);
  } else {
    // Any recognized gesture on a control should still kill the click.
    markSwipe(indev);
  }
}

}  // namespace

bool workspacePagerCreate() {
  const DeckProfile& profile = profileStore.profile();
  s_page_count = profile.page_count;
  if (s_page_count < DECK_PAGE_MIN) s_page_count = DECK_PAGE_MIN;
  if (s_page_count > DECK_PAGE_MAX) s_page_count = DECK_PAGE_MAX;

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), 0);
  s_root = scr;

  s_media_host = lv_obj_create(scr);
  lv_obj_set_size(s_media_host, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
  lv_obj_set_style_bg_color(s_media_host, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(s_media_host, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_media_host, 0, 0);
  lv_obj_set_style_pad_all(s_media_host, 0, 0);
  lv_obj_clear_flag(s_media_host, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_media_host, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(s_media_host, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_media_host, onGesture, LV_EVENT_GESTURE, nullptr);
  if (!mediaScreenCreate(s_media_host)) {
    return false;
  }

  s_grid_host = lv_obj_create(scr);
  lv_obj_set_size(s_grid_host, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
  lv_obj_set_style_bg_color(s_grid_host, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(s_grid_host, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_grid_host, 0, 0);
  lv_obj_set_style_pad_all(s_grid_host, 0, 0);
  lv_obj_clear_flag(s_grid_host, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_grid_host, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(s_grid_host, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_grid_host, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_grid_host, onGesture, LV_EVENT_GESTURE, nullptr);
  if (!homeGridScreenCreateOn(s_grid_host)) {
    return false;
  }
  s_loaded_shortcut = 0;
  s_shortcut_idx = 0;

  s_dot_row = lv_obj_create(scr);
  lv_obj_set_size(s_dot_row, 200, 28);
  lv_obj_set_style_bg_opa(s_dot_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_dot_row, 0, 0);
  lv_obj_set_style_pad_all(s_dot_row, 0, 0);
  lv_obj_set_flex_flow(s_dot_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_dot_row, 10, 0);
  lv_obj_align(s_dot_row, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_clear_flag(s_dot_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_FLOATING);

  for (uint8_t i = 0; i < DECK_PAGE_MAX; ++i) {
    s_dots[i] = lv_obj_create(s_dot_row);
    lv_obj_set_size(s_dots[i], 12, 12);
    lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_dots[i], 0, 0);
    lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }

  uint8_t start = loadSavedPage();
  if (start >= s_page_count) start = 0;
  applyPage(start, false);
  Serial.printf("[UI] Workspace pager ready pages=%u (snap)\n", s_page_count);
  return true;
}

void workspacePagerTick() {
  mediaScreenTick();
  if (!s_on_media) {
    homeGridScreenTick();
  }
  if (s_page_dirty && static_cast<int32_t>(millis() - s_save_at_ms) >= 0) {
    savePageNow();
  }
}

void workspacePagerSetPage(uint8_t index) {
  if (!displayDriverLock(200)) return;
  applyPage(index, false);
  displayDriverUnlock();
}

uint8_t workspacePagerPage() { return s_page; }

uint8_t workspacePagerPageCount() { return s_page_count; }

bool workspacePagerSetPageCount(uint8_t total_pages) {
  static DeckProfile profile;
  profile = profileStore.profile();
  if (total_pages < DECK_PAGE_MIN) total_pages = DECK_PAGE_MIN;
  if (total_pages > DECK_PAGE_MAX) total_pages = DECK_PAGE_MAX;
  profile.page_count = total_pages;
  deckProfileEnsureShortcutCount(profile);
  profile.rev++;
  if (!profileStore.save(profile)) {
    return false;
  }
  s_page_count = total_pages;
  if (s_page >= s_page_count) {
    s_page = static_cast<uint8_t>(s_page_count - 1);
  }
  s_loaded_shortcut = -1;
  if (!displayDriverLock(300)) return false;
  applyPage(s_page, false);
  displayDriverUnlock();
  return true;
}

bool workspacePagerReloadShortcuts() {
  if (s_on_media) {
    return true;
  }
  s_loaded_shortcut = -1;
  if (!displayDriverLock(300)) return false;
  const bool ok = showShortcutLocked(s_shortcut_idx);
  displayDriverUnlock();
  return ok;
}

void workspacePagerSyncFromStore() {
  const DeckProfile& profile = profileStore.profile();
  s_page_count = profile.page_count;
  if (s_page_count < DECK_PAGE_MIN) s_page_count = DECK_PAGE_MIN;
  if (s_page_count > DECK_PAGE_MAX) s_page_count = DECK_PAGE_MAX;
  if (s_page >= s_page_count) {
    s_page = static_cast<uint8_t>(s_page_count - 1);
  }
  s_loaded_shortcut = -1;
  if (!displayDriverLock(300)) return;
  applyPage(s_page, false);
  displayDriverUnlock();
}

bool workspacePagerSwipeSuppress() {
  return s_drag_is_swipe || (s_swipe_suppress_until_ms != 0 && millis() < s_swipe_suppress_until_ms);
}

bool workspacePagerTouchGate(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev && code != LV_EVENT_CLICKED) {
    return workspacePagerSwipeSuppress();
  }

  if (code == LV_EVENT_PRESSED) {
    s_press_tracking = true;
    s_drag_is_swipe = false;
    s_swipe_page_applied = false;
    if (indev) {
      lv_indev_get_point(indev, &s_press_pt);
    }
    return false;
  }

  if (code == LV_EVENT_PRESSING && s_press_tracking && indev) {
    lv_point_t cur{};
    lv_indev_get_point(indev, &cur);
    const lv_coord_t dx = static_cast<lv_coord_t>(cur.x - s_press_pt.x);
    const lv_coord_t dy = static_cast<lv_coord_t>(cur.y - s_press_pt.y);
    if (!s_drag_is_swipe) {
      const lv_coord_t adx = dx < 0 ? static_cast<lv_coord_t>(-dx) : dx;
      const lv_coord_t ady = dy < 0 ? static_cast<lv_coord_t>(-dy) : dy;
      // Horizontal-dominant drag → page swipe (even when press started on a button).
      if (adx >= kSwipeMinDx && adx > ady + 8) {
        tryApplySwipeFromDx(dx, indev);
        if (!s_swipe_page_applied) {
          // At edge: still treat as swipe so the button under the finger doesn't fire.
          markSwipe(indev);
        }
      }
    }
    return s_drag_is_swipe;
  }

  if (code == LV_EVENT_CLICKED) {
    bool ignore = workspacePagerSwipeSuppress();
    if (!ignore && s_press_tracking && indev) {
      lv_point_t cur{};
      lv_indev_get_point(indev, &cur);
      const lv_coord_t dx = static_cast<lv_coord_t>(cur.x - s_press_pt.x);
      const lv_coord_t dy = static_cast<lv_coord_t>(cur.y - s_press_pt.y);
      const lv_coord_t adx = dx < 0 ? static_cast<lv_coord_t>(-dx) : dx;
      const lv_coord_t ady = dy < 0 ? static_cast<lv_coord_t>(-dy) : dy;
      if (adx > kClickMaxSlop || ady > kClickMaxSlop) {
        ignore = true;
      }
      // Late horizontal drag that never hit PRESSING threshold path.
      if (!ignore && adx >= kSwipeMinDx && adx > ady) {
        tryApplySwipeFromDx(dx, indev);
        ignore = true;
      }
    }
    s_press_tracking = false;
    return ignore;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    s_press_tracking = false;
  }
  // Do not clear on RELEASED — LVGL emits CLICKED after RELEASED and needs the press point.
  return workspacePagerSwipeSuppress();
}

lv_obj_t* workspacePagerRoot() { return s_root; }
