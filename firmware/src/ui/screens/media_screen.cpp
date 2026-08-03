#include "media_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/input_queue.h"
#include "app_config.h"
#include "ble/ble_manager.h"
#include "display/display_driver.h"
#include "grid_config.h"
#include "system/idle_manager.h"
#include "ui/fonts/ui_fonts_vi.h"
#include "ui/screens/deck_header.h"
#include "ui/screens/workspace_pager.h"
#include "ui/screens/home_grid_screen.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_artist = nullptr;
lv_obj_t* s_app = nullptr;
lv_obj_t* s_progress = nullptr;
lv_obj_t* s_time = nullptr;
lv_obj_t* s_play = nullptr;
lv_obj_t* s_rate = nullptr;
lv_obj_t* s_mute = nullptr;
bool s_linked = false;
bool s_playing = false;
bool s_has_track = false;
uint32_t s_pos_ms = 0;
uint32_t s_dur_ms = 0;
uint32_t s_pos_sync_ms = 0;
uint16_t s_rate_x100 = 100;
int s_volume = APP_VOLUME_DEFAULT;
bool s_muted = false;
uint32_t s_last_press_ms = 0;
char s_last_press_id[GRID_ACTION_ID_MAX + 1] = {};
char s_title_buf[96] = {};
char s_artist_buf[96] = {};
lv_timer_t* s_progress_timer = nullptr;

constexpr uint32_t kPressDebounceMs = 180;
constexpr uint32_t kProgressTimerMs = 200;
// Host ahead of local → seek / catch-up. Never snap back to a stale lower host pos
// unless the host itself jumped backward (real seek-back).
constexpr int32_t kPosResyncAheadMs = 1500;
constexpr int32_t kHostSeekBackMs = 1500;

uint32_t s_last_host_pos_ms = 0;
bool s_have_host_pos = false;

uint32_t displayPosMs() {
  if (!s_playing || s_dur_ms == 0) {
    return s_pos_ms;
  }
  const uint32_t now = millis();
  const uint32_t dt = (now >= s_pos_sync_ms) ? (now - s_pos_sync_ms) : 0;
  uint64_t elapsed = static_cast<uint64_t>(s_pos_ms) +
                     (static_cast<uint64_t>(dt) * s_rate_x100) / 100ULL;
  if (s_dur_ms > 0 && elapsed > s_dur_ms) {
    elapsed = s_dur_ms;
  }
  return static_cast<uint32_t>(elapsed);
}

void hardSyncPos(uint32_t pos_ms) {
  s_pos_ms = pos_ms;
  s_pos_sync_ms = millis();
}

void noteHostPos(uint32_t pos_ms) {
  s_last_host_pos_ms = pos_ms;
  s_have_host_pos = true;
}

/** Apply host position carefully while already playing the same track. */
void mergeHostPosWhilePlaying(uint32_t pos_ms) {
  const uint32_t local = displayPosMs();
  const int32_t delta = static_cast<int32_t>(pos_ms) - static_cast<int32_t>(local);

  // Host jumped backward vs previous host sample → user seeked back on Mac.
  if (s_have_host_pos && pos_ms + kHostSeekBackMs < s_last_host_pos_ms) {
    hardSyncPos(pos_ms);
    noteHostPos(pos_ms);
    return;
  }

  if (delta > kPosResyncAheadMs) {
    // Host ahead of local clock → seek forward / catch-up.
    hardSyncPos(pos_ms);
  }
  // Host equal/behind: keep local interpolation (stale JXA pos often sticks at 0).
  noteHostPos(pos_ms);
}

void refreshProgress();

void onProgressTimer(lv_timer_t* /*t*/) {
  if (!s_playing || s_dur_ms == 0 || !s_progress) {
    return;
  }
  refreshProgress();
}

void refreshMuteBtn() {
  if (!s_mute) {
    return;
  }
  lv_obj_t* lab = lv_obj_get_child(s_mute, 0);
  if (s_muted) {
    lv_obj_set_style_bg_color(s_mute, lv_color_hex(0x7F1D1D), 0);
    lv_obj_set_style_bg_color(s_mute, lv_color_hex(0x991B1B), LV_STATE_PRESSED);
    if (lab) {
      lv_label_set_text(lab, LV_SYMBOL_MUTE);
      lv_obj_set_style_text_color(lab, lv_color_hex(0xFCA5A5), 0);
    }
  } else {
    lv_obj_set_style_bg_color(s_mute, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_color(s_mute, lv_color_hex(0x334155), LV_STATE_PRESSED);
    if (lab) {
      lv_label_set_text(lab, LV_SYMBOL_VOLUME_MID);
      lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), 0);
    }
  }
}

void refreshProgress() {
  if (!s_progress || !s_time) {
    return;
  }
  const uint32_t pos = displayPosMs();
  int pct = 0;
  if (s_dur_ms > 0) {
    pct = static_cast<int>((pos * 100ULL) / s_dur_ms);
    if (pct > 100) pct = 100;
  }
  lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);
  char buf[32];
  auto fmt = [](uint32_t ms, char* out, size_t n) {
    const uint32_t sec = ms / 1000;
    snprintf(out, n, "%u:%02u", sec / 60, sec % 60);
  };
  char a[12], b[12];
  fmt(pos, a, sizeof(a));
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
  refreshMuteBtn();
}

void applyOptimisticVolume(const char* action_id) {
  if (!action_id) {
    return;
  }
  if (strcmp(action_id, "volume_up") == 0) {
    s_muted = false;
    s_volume = min(APP_VOLUME_MAX, s_volume + APP_VOLUME_STEP);
  } else if (strcmp(action_id, "volume_down") == 0) {
    s_muted = false;
    s_volume = max(APP_VOLUME_MIN, s_volume - APP_VOLUME_STEP);
  } else if (strcmp(action_id, "mute") == 0) {
    s_muted = !s_muted;
  } else {
    return;
  }
  // Keep header + shortcut mute tile in sync (homeGrid also refreshes deck header).
  homeGridScreenSetVolume(s_volume, s_muted);
  refreshMuteBtn();
}

void applyOptimisticSeek(const char* action_id) {
  if (!action_id || s_dur_ms == 0) {
    return;
  }
  int32_t delta = 0;
  if (strcmp(action_id, "media_seek_fwd") == 0) {
    delta = 10000;
  } else if (strcmp(action_id, "media_seek_back") == 0) {
    delta = -10000;
  } else {
    return;
  }
  int64_t next = static_cast<int64_t>(displayPosMs()) + delta;
  if (next < 0) next = 0;
  if (next > static_cast<int64_t>(s_dur_ms)) next = s_dur_ms;
  hardSyncPos(static_cast<uint32_t>(next));
  noteHostPos(static_cast<uint32_t>(next));
  refreshProgress();
}

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

  applyOptimisticVolume(action_id);
  applyOptimisticSeek(action_id);

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
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_RELEASED, const_cast<char*>(action_id));
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_PRESS_LOST, const_cast<char*>(action_id));
  lv_obj_add_event_cb(btn, onBtn, LV_EVENT_CLICKED, const_cast<char*>(action_id));
  lv_obj_t* lab = lv_label_create(btn);
  lv_label_set_text(lab, label);
  lv_obj_set_style_text_font(lab, &lv_font_montserrat_20, 0);
  lv_obj_clear_flag(lab, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(lab);
  return btn;
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
  lv_obj_set_style_pad_left(s_root, 20, 0);
  lv_obj_set_style_pad_right(s_root, 20, 0);
  lv_obj_set_style_pad_bottom(s_root, 20, 0);
  lv_obj_set_style_pad_top(s_root, deckHeaderHeight() + 8, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(s_root);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_root, onBtn, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(s_root, onBtn, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(s_root, onBtn, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(s_root, onBtn, LV_EVENT_PRESS_LOST, nullptr);

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

  // Rate + volume on one row: [ - ] [ 1x ] [ + ]     [ Vol- ] [ Mute ] [ Vol+ ]
  lv_obj_t* bottom = lv_obj_create(s_root);
  lv_obj_set_size(bottom, 760, 70);
  lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bottom, 0, 0);
  lv_obj_set_style_pad_all(bottom, 0, 0);
  lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(bottom, LV_ALIGN_TOP_MID, 0, 310);
  lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(bottom);

  lv_obj_t* speed = lv_obj_create(bottom);
  lv_obj_set_size(speed, 220, 56);
  lv_obj_set_style_bg_opa(speed, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(speed, 0, 0);
  lv_obj_set_style_pad_all(speed, 0, 0);
  lv_obj_set_flex_flow(speed, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(speed, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(speed, 8, 0);
  lv_obj_clear_flag(speed, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(speed);
  makeBtn(speed, "-", "media_rate_down", 56);
  s_rate = makeBtn(speed, "1x", "media_rate_1x", 80);
  makeBtn(speed, "+", "media_rate_up", 56);

  lv_obj_t* vol = lv_obj_create(bottom);
  lv_obj_set_size(vol, 500, 56);
  lv_obj_set_style_bg_opa(vol, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vol, 0, 0);
  lv_obj_set_style_pad_all(vol, 0, 0);
  lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(vol, 10, 0);
  lv_obj_clear_flag(vol, LV_OBJ_FLAG_SCROLLABLE);
  enableGestureBubble(vol);
  makeBtn(vol, LV_SYMBOL_VOLUME_MID " -", "volume_down", 150);
  s_mute = makeBtn(vol, LV_SYMBOL_VOLUME_MID, "mute", 120);
  makeBtn(vol, LV_SYMBOL_VOLUME_MAX " +", "volume_up", 150);

  refreshChrome();
  refreshProgress();
  if (!s_progress_timer) {
    s_progress_timer = lv_timer_create(onProgressTimer, kProgressTimerMs, nullptr);
  }
  return true;
}

void mediaScreenSetNowPlaying(const char* title, const char* artist, bool playing, uint32_t pos_ms,
                              uint32_t dur_ms, const char* app, uint16_t rate_x100) {
  if (!displayDriverLock(200)) {
    return;
  }

  const bool had_track = s_has_track;
  const bool was_playing = s_playing;
  char prev_title[96];
  memcpy(prev_title, s_title_buf, sizeof(prev_title));

  s_playing = playing;
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

  const bool track_changed =
      !had_track || !s_has_track || strncmp(prev_title, s_title_buf, sizeof(prev_title)) != 0;

  // Host Now Playing pos is often stale (stuck at 0). Keep local clock while playing
  // the same track; only hard-sync on pause/resume/track change/seek-forward/host seek-back.
  if (!playing || track_changed || !was_playing) {
    hardSyncPos(pos_ms);
    noteHostPos(pos_ms);
  } else {
    mergeHostPosWhilePlaying(pos_ms);
  }

  if (s_title) {
    lv_label_set_text(s_title, s_has_track ? s_title_buf : "-");
  }
  if (s_artist) {
    lv_label_set_text(s_artist, s_artist_buf);
  }
  if (s_app) {
    lv_label_set_text(s_app, (app && app[0]) ? app : "");
  }
  refreshProgress();
  refreshChrome();
  displayDriverUnlock();
}

void mediaScreenSetVolume(int volume, bool muted) {
  s_volume = constrain(volume, APP_VOLUME_MIN, APP_VOLUME_MAX);
  s_muted = muted;
  // Header + mute tiles are updated by homeGridScreenSetVolume in the BLE path.
  // When called alone, still refresh media mute chrome.
  if (displayDriverLock(50)) {
    refreshMuteBtn();
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
  // Progress UI is driven by LVGL timer (onProgressTimer) so it does not contend
  // with the display mutex from the app loop.
}

bool mediaScreenIsPlaying() { return s_playing && s_has_track; }

const char* mediaScreenTitle() { return s_title_buf; }

const char* mediaScreenArtist() { return s_artist_buf; }

lv_obj_t* mediaScreenRoot() { return s_root; }
