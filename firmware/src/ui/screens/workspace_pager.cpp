#include "workspace_pager.h"

#include <Preferences.h>
#include <string.h>

#include "board_config.h"
#include "deck_header.h"
#include "display/display_driver.h"
#include "home_grid_screen.h"
#include "media_screen.h"
#include "storage/profile_store.h"
#include "ui/touch_router.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_page_hosts[DECK_PAGE_MAX] = {};
lv_obj_t* s_dots[DECK_PAGE_MAX] = {};
lv_obj_t* s_dot_row = nullptr;
uint8_t s_page_count = DECK_PAGE_MIN;
uint8_t s_page = 0;
bool s_page_dirty = false;
uint32_t s_save_at_ms = 0;

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

void raiseChrome() {
  deckHeaderRaise();
  if (s_dot_row) {
    lv_obj_move_foreground(s_dot_row);
  }
}

void showOnlyPage(uint8_t page) {
  for (uint8_t i = 0; i < DECK_PAGE_MAX; ++i) {
    if (!s_page_hosts[i]) continue;
    if (i == page && i < s_page_count) {
      lv_obj_clear_flag(s_page_hosts[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_page_hosts[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  raiseChrome();
}

void applyPage(uint8_t page) {
  if (page >= s_page_count) {
    page = static_cast<uint8_t>(s_page_count - 1);
  }
  s_page = page;
  showOnlyPage(page);
  refreshDots();
  scheduleSavePage();
}

void onPageStep(int8_t step) {
  if (step > 0) {
    if (s_page + 1 < s_page_count) {
      applyPage(static_cast<uint8_t>(s_page + 1));
    }
  } else if (step < 0) {
    if (s_page > 0) {
      applyPage(static_cast<uint8_t>(s_page - 1));
    }
  }
}

void onDot(lv_event_t* e) {
  if (touchRouterHandleEvent(e)) {
    return;
  }
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  applyPage(static_cast<uint8_t>(idx));
}

void onHostTouch(lv_event_t* e) {
  // Empty areas / gesture bubble path — feed router so swipes not on a button still work.
  (void)touchRouterHandleEvent(e);
}

lv_obj_t* makePageHost(lv_obj_t* parent) {
  lv_obj_t* host = lv_obj_create(parent);
  lv_obj_set_size(host, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
  lv_obj_set_pos(host, 0, 0);
  lv_obj_set_style_bg_color(host, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(host, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(host, 0, 0);
  lv_obj_set_style_pad_all(host, 0, 0);
  lv_obj_set_style_radius(host, 0, 0);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(host, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(host, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(host, onHostTouch, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(host, onHostTouch, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(host, onHostTouch, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(host, onHostTouch, LV_EVENT_PRESS_LOST, nullptr);
  return host;
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

  touchRouterBegin();
  touchRouterSetPageStepHandler(onPageStep);

  s_page_hosts[0] = makePageHost(scr);
  if (!mediaScreenCreate(s_page_hosts[0])) {
    return false;
  }

  for (uint8_t i = 1; i < DECK_PAGE_MAX; ++i) {
    s_page_hosts[i] = makePageHost(scr);
    if (!homeGridScreenCreateAt(s_page_hosts[i], static_cast<uint8_t>(i - 1))) {
      return false;
    }
    lv_obj_add_flag(s_page_hosts[i], LV_OBJ_FLAG_HIDDEN);
  }

  if (!deckHeaderCreate(scr)) {
    return false;
  }

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
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_PRESSED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_PRESSING, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_RELEASED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_PRESS_LOST, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(s_dots[i], onDot, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }

  uint8_t start = loadSavedPage();
  if (start >= s_page_count) start = 0;
  applyPage(start);
  Serial.printf("[UI] Workspace pager ready pages=%u (touch-router + shared header)\n", s_page_count);
  return true;
}

void workspacePagerTick() {
  mediaScreenTick();
  homeGridScreenTick();
  deckHeaderTick();
  if (s_page_dirty && static_cast<int32_t>(millis() - s_save_at_ms) >= 0) {
    savePageNow();
  }
}

void workspacePagerSetPage(uint8_t index) {
  if (!displayDriverLock(200)) return;
  applyPage(index);
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
  if (!displayDriverLock(300)) return false;
  for (uint8_t i = 0; i < profile.shortcut_count && i < DECK_SHORTCUT_PAGES_MAX; ++i) {
    homeGridScreenReloadPageLocked(i, profile.pages[i]);
  }
  applyPage(s_page);
  displayDriverUnlock();
  return true;
}

bool workspacePagerReloadShortcuts() {
  if (!displayDriverLock(300)) return false;
  const DeckProfile& profile = profileStore.profile();
  bool ok = true;
  for (uint8_t i = 0; i < profile.shortcut_count && i < DECK_SHORTCUT_PAGES_MAX; ++i) {
    if (!homeGridScreenReloadPageLocked(i, profile.pages[i])) {
      ok = false;
    }
  }
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
  if (!displayDriverLock(300)) return;
  for (uint8_t i = 0; i < profile.shortcut_count && i < DECK_SHORTCUT_PAGES_MAX; ++i) {
    homeGridScreenReloadPageLocked(i, profile.pages[i]);
  }
  applyPage(s_page);
  displayDriverUnlock();
}

bool workspacePagerSwipeSuppress() {
  return touchRouterSuppressesClick();
}

bool workspacePagerTouchGate(lv_event_t* e) {
  return touchRouterHandleEvent(e);
}

lv_obj_t* workspacePagerRoot() { return s_root; }
