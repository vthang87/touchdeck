#include "media_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/input_queue.h"
#include "ble/ble_manager.h"
#include "display/display_driver.h"
#include "grid_config.h"
#include "system/idle_manager.h"
#include "ui/fonts/ui_fonts_vi.h"
#include "ui/screens/workspace_pager.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_artist = nullptr;
lv_obj_t* s_app = nullptr;
lv_obj_t* s_progress = nullptr;
lv_obj_t* s_time = nullptr;
lv_obj_t* s_play = nullptr;
lv_obj_t* s_rate = nullptr;
lv_obj_t* s_hint = nullptr;
bool s_linked = false;
bool s_playing = false;
bool s_has_track = false;
uint32_t s_pos_ms = 0;
uint32_t s_dur_ms = 0;
uint16_t s_rate_x100 = 100;
int s_volume = 50;
bool s_muted = false;
uint32_t s_last_press_ms = 0;
char s_last_press_id[GRID_ACTION_ID_MAX + 1] = {};
char s_title_buf[96] = {};
char s_artist_buf[96] = {};

constexpr uint32_t kPressDebounceMs = 180;

void pushAction(const char* action_id) {
  if (!action_id || !action_id[0]) {
    return;
  }
  if (workspacePagerSwipeSuppress()) {
    return;
  }
  if (!idleManagerAllowTilePress()) {
    return;
  }
  if (!bleManagerIsConnected()) {
    return;
  }
  const uint32_t now = millis();
  if (s_last_press_id[0] && strcmp(s_last_press_id, action_id) == 0 &&
      (now - s_last_press_ms) < kPressDebounceMs) {
    return;
  }
  s_last_press_ms = now;
  strncpy(s_last_press_id, action_id, GRID_ACTION_ID_MAX);
  s_last_press_id[GRID_ACTION_ID_MAX] = '\0';

  GridTile tile{};
  strncpy(tile.id, action_id, GRID_ID_MAX);
  strncpy(tile.label, action_id, GRID_LABEL_MAX);
  strncpy(tile.action_id, action_id, GRID_ACTION_ID_MAX);
  if (strcmp(action_id, "volume_up") == 0) {
    tile.action = TileAction::VolumeUp;
  } else if (strcmp(action_id, "volume_down") == 0) {
    tile.action = TileAction::VolumeDown;
  } else if (strcmp(action_id, "mute") == 0) {
    tile.action = TileAction::Mute;
  } else if (strcmp(action_id, "media_play_pause") == 0 || strcmp(action_id, "play_pause") == 0) {
    tile.action = TileAction::PlayPause;
  } else if (strcmp(action_id, "media_next") == 0 || strcmp(action_id, "next") == 0) {
    tile.action = TileAction::Next;
  } else if (strcmp(action_id, "media_previous") == 0 || strcmp(action_id, "previous") == 0) {
    tile.action = TileAction::Previous;
  } else {
    tile.action = TileAction::App;  // seek / rate etc. still routed by action_id
  }
  inputQueue.pushTile(tile);
}

void onBtn(lv_event_t* e) {
  // Drag-aware gate: horizontal swipe from a button changes page and kills the click.
  if (workspacePagerTouchGate(e)) {
    return;
  }
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const char* id = static_cast<const char*>(lv_event_get_user_data(e));
  if (id) {
    pushAction(id);
  }
}

void enableGestureBubble(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

lv_obj_t* makeBtn(lv_obj_t* parent, const char* label, const char* action_id, lv_coord_t w) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, 56);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), LV_STATE_PRESSED);
  // Let horizontal swipes bubble to the workspace pager host.
  enableGestureBubble(btn);
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_PRESSED, const_cast<char*>(action_id));
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_PRESSING, const_cast<char*>(action_id));
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_CLICKED, const_cast<char*>(action_id));
  lv_obj_t* lab = lv_label_create(btn);
  lv_label_set_text(lab, label);
  lv_obj_set_style_text_font(lab, &lv_font_montserrat_20, 0);
  lv_obj_clear_flag(lab, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(lab);
  return btn;
}

void refreshProgress() {
  if (!s_progress || !s_time) {
    return;
  }
  int pct = 0;
  if (s_dur_ms > 0) {
    pct = static_cast<int>((s_pos_ms * 100ULL) / s_dur_ms);
    if (pct > 100) pct = 100;
  }
  lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);
  char buf[32];
  auto fmt = [](uint32_t ms, char* out, size_t n) {
    const uint32_t sec = ms / 1000;
    snprintf(out, n, "%u:%02u", sec / 60, sec % 60);
  };
  char a[12], b[12];
  fmt(s_pos_ms, a, sizeof(a));
  fmt(s_dur_ms, b, sizeof(b));
  snprintf(buf, sizeof(buf), "%s / %s", a, b);
  lv_label_set_text(s_time, buf);
  if (s_rate) {
    char rb[16];
    const unsigned whole = s_rate_x100 / 100;
    const unsigned frac = (s_rate_x100 % 100) / 10;
    if (frac == 0) {
      snprintf(rb, sizeof(rb), "%ux", whole);
    } else {
      snprintf(rb, sizeof(rb), "%u.%ux", whole, frac);
    }
    lv_label_set_text(lv_obj_get_child(s_rate, 0), rb);
  }
}

void refreshChrome() {
  if (s_play) {
    lv_label_set_text(lv_obj_get_child(s_play, 0), s_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  }
  if (s_hint) {
    if (!s_linked) {
      lv_label_set_text(s_hint, "Connect Companion for Now Playing");
    } else if (!s_has_track) {
      lv_label_set_text(s_hint, "Nothing playing");
    } else {
      lv_label_set_text(s_hint, s_muted ? "Muted" : "");
    }
  }
  // Don't dim the whole page — linked/empty is shown via hint text only.
  (void)s_linked;
}

}  // namespace

bool mediaScreenCreate(lv_obj_t* parent) {
  if (!parent) {
    return false;
  }
  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_root, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_root, 0, 0);
  lv_obj_set_style_pad_all(s_root, 20, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(s_root);

  lv_obj_t* heading = lv_label_create(s_root);
  lv_label_set_text(heading, "Now Playing");
  lv_obj_set_style_text_font(heading, &ui_font_vi_14, 0);
  lv_obj_set_style_text_color(heading, lv_color_hex(0x64748B), 0);
  lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);

  s_app = lv_label_create(s_root);
  lv_label_set_text(s_app, "");
  lv_obj_set_style_text_font(s_app, &ui_font_vi_14, 0);
  lv_obj_set_style_text_color(s_app, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(s_app, LV_ALIGN_TOP_RIGHT, 0, 0);

  s_title = lv_label_create(s_root);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_title, 720);
  lv_label_set_text(s_title, "-");
  lv_obj_set_style_text_font(s_title, &ui_font_vi_28, 0);
  lv_obj_set_style_text_color(s_title, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 48);

  s_artist = lv_label_create(s_root);
  lv_label_set_long_mode(s_artist, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_artist, 720);
  lv_label_set_text(s_artist, "");
  lv_obj_set_style_text_font(s_artist, &ui_font_vi_20, 0);
  lv_obj_set_style_text_color(s_artist, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(s_artist, LV_ALIGN_TOP_LEFT, 0, 92);

  s_progress = lv_bar_create(s_root);
  lv_obj_set_size(s_progress, 720, 10);
  lv_bar_set_range(s_progress, 0, 100);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_align(s_progress, LV_ALIGN_TOP_LEFT, 0, 150);

  s_time = lv_label_create(s_root);
  lv_label_set_text(s_time, "0:00 / 0:00");
  lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_time, lv_color_hex(0x64748B), 0);
  lv_obj_align(s_time, LV_ALIGN_TOP_LEFT, 0, 168);

  lv_obj_t* speed = lv_obj_create(s_root);
  lv_obj_set_size(speed, 240, 56);
  lv_obj_set_style_bg_opa(speed, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(speed, 0, 0);
  lv_obj_set_style_pad_all(speed, 0, 0);
  lv_obj_set_flex_flow(speed, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(speed, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(speed, 8, 0);
  lv_obj_align(speed, LV_ALIGN_TOP_RIGHT, 0, 150);
  lv_obj_clear_flag(speed, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(speed);
  makeBtn(speed, "-", "media_rate_down", 56);
  s_rate = makeBtn(speed, "1x", "media_rate_1x", 80);
  makeBtn(speed, "+", "media_rate_up", 56);

  lv_obj_t* row = lv_obj_create(s_root);
  lv_obj_set_size(row, 760, 70);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 220);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(row);

  makeBtn(row, "-10s", "media_seek_back", 110);
  makeBtn(row, LV_SYMBOL_PREV, "media_previous", 110);
  s_play = makeBtn(row, LV_SYMBOL_PLAY, "media_play_pause", 120);
  makeBtn(row, LV_SYMBOL_NEXT, "media_next", 110);
  makeBtn(row, "+10s", "media_seek_fwd", 110);

  lv_obj_t* vol = lv_obj_create(s_root);
  lv_obj_set_size(vol, 760, 70);
  lv_obj_set_style_bg_opa(vol, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vol, 0, 0);
  lv_obj_set_style_pad_all(vol, 0, 0);
  lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(vol, LV_ALIGN_TOP_MID, 0, 310);
  lv_obj_clear_flag(vol, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(vol);
  makeBtn(vol, LV_SYMBOL_VOLUME_MID " -", "volume_down", 160);
  makeBtn(vol, LV_SYMBOL_MUTE, "mute", 160);
  makeBtn(vol, LV_SYMBOL_VOLUME_MAX " +", "volume_up", 160);

  s_hint = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_hint, &ui_font_vi_14, 0);
  lv_obj_set_style_text_color(s_hint, lv_color_hex(0x64748B), 0);
  lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  lv_obj_t* swipe_hint = lv_label_create(s_root);
  lv_label_set_text(swipe_hint, "Swipe for shortcuts");
  lv_obj_set_style_text_font(swipe_hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(swipe_hint, lv_color_hex(0x475569), 0);
  lv_obj_align(swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -28);

  refreshChrome();
  refreshProgress();
  return true;
}

void mediaScreenSetNowPlaying(const char* title, const char* artist, bool playing, uint32_t pos_ms,
                              uint32_t dur_ms, const char* app, uint16_t rate_x100) {
  if (!displayDriverLock(200)) {
    return;
  }
  s_playing = playing;
  s_pos_ms = pos_ms;
  s_dur_ms = dur_ms;
  s_rate_x100 = rate_x100 == 0 ? 100 : rate_x100;
  s_has_track = title && title[0];
  if (s_has_track) {
    strncpy(s_title_buf, title, sizeof(s_title_buf) - 1);
    s_title_buf[sizeof(s_title_buf) - 1] = '\0';
  } else {
    s_title_buf[0] = '\0';
  }
  if (artist && artist[0]) {
    strncpy(s_artist_buf, artist, sizeof(s_artist_buf) - 1);
    s_artist_buf[sizeof(s_artist_buf) - 1] = '\0';
  } else {
    s_artist_buf[0] = '\0';
  }
  if (s_title) {
    lv_label_set_text(s_title, s_has_track ? s_title_buf : "-");
  }
  if (s_artist) {
    lv_label_set_text(s_artist, s_artist_buf);
  }
  if (s_app) {
    lv_label_set_text(s_app, app && app[0] ? app : "");
  }
  refreshProgress();
  refreshChrome();
  displayDriverUnlock();
}

void mediaScreenSetVolume(int volume, bool muted) {
  s_volume = volume;
  s_muted = muted;
  if (displayDriverLock(50)) {
    refreshChrome();
    displayDriverUnlock();
  }
}

void mediaScreenSetLinked(bool linked) {
  s_linked = linked;
  if (displayDriverLock(50)) {
    refreshChrome();
    displayDriverUnlock();
  }
}

void mediaScreenTick() {
  // Progress is host-driven; nothing to animate locally for now.
}

bool mediaScreenIsPlaying() { return s_playing && s_has_track; }

const char* mediaScreenTitle() { return s_title_buf; }

const char* mediaScreenArtist() { return s_artist_buf; }

lv_obj_t* mediaScreenRoot() { return s_root; }
