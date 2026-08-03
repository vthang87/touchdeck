#include "deck_header.h"

#include <time.h>

#include "app_config.h"
#include "ble/ble_manager.h"
#include "board_config.h"
#include "display/display_driver.h"
#include "storage/settings_store.h"
#include "system/system_status.h"
#include "ui/icons/icon_link_arrows.h"

namespace {

lv_obj_t* s_root = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_wifi = nullptr;
lv_obj_t* s_ble = nullptr;
lv_obj_t* s_ws = nullptr;
lv_obj_t* s_volume = nullptr;
lv_obj_t* s_clock = nullptr;
int s_volume_level = APP_VOLUME_DEFAULT;
bool s_muted = false;
bool s_volume_dirty = false;
uint32_t s_last_ms = 0;

constexpr lv_coord_t kHeaderH = 56;

void layoutStatus() {
  if (s_wifi) {
    lv_obj_align(s_wifi, LV_ALIGN_RIGHT_MID, -16, 0);
  }
  if (s_ws && s_wifi) {
    lv_obj_align_to(s_ws, s_wifi, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  }
  if (s_ble && s_ws) {
    lv_obj_align_to(s_ble, s_ws, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  }
  if (s_volume && s_ble) {
    lv_obj_align_to(s_volume, s_ble, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  }
  if (s_clock && s_volume) {
    lv_obj_align_to(s_clock, s_volume, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  }
}

void refreshVolume() {
  if (!s_volume) {
    return;
  }
  if (s_muted) {
    lv_label_set_text(s_volume, LV_SYMBOL_MUTE "  Muted");
    lv_obj_set_style_text_color(s_volume, lv_color_hex(0xF87171), 0);
  } else {
    lv_label_set_text_fmt(s_volume, LV_SYMBOL_VOLUME_MAX "  %d%%", s_volume_level);
    lv_obj_set_style_text_color(s_volume, lv_color_hex(0xE2E8F0), 0);
  }
  // Force relayout — % width changes (9% → 100%, Muted) and can leave stale glyphs.
  lv_obj_mark_layout_as_dirty(s_root);
}

void refreshClock() {
  if (!s_clock) {
    return;
  }
  time_t now = time(nullptr);
  struct tm local {};
  if (now < 1700000000 || !localtime_r(&now, &local)) {
    lv_label_set_text(s_clock, "--:--");
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0x64748B), 0);
    return;
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &local);
  lv_label_set_text(s_clock, buf);
  lv_obj_set_style_text_color(s_clock, lv_color_hex(0xE2E8F0), 0);
}

void refreshConn() {
  const SystemSnapshot snap = systemStatus.snapshot();
  if (s_wifi) {
    switch (snap.wifi) {
      case SystemWifiState::Connected:
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(0x38BDF8), 0);
        break;
      case SystemWifiState::ApMode:
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(0xFBBF24), 0);
        break;
      case SystemWifiState::Connecting:
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(0x64748B), 0);
        break;
      default:
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(0x475569), 0);
        break;
    }
  }
  if (s_ble) {
    // Always icon-only — no "pair" / "idle" / "off" suffix.
    lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH);
    if (!bleManagerIsStarted()) {
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x475569), 0);
    } else if (bleManagerIsConnected()) {
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x4ADE80), 0);
    } else if (bleManagerPairMode()) {
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0xFBBF24), 0);
    } else {
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x64748B), 0);
    }
  }
  if (s_ws) {
    lv_obj_set_style_img_recolor(
        s_ws, bleManagerIsConnected() ? lv_color_hex(0x4ADE80) : lv_color_hex(0x64748B), 0);
  }
  refreshClock();
  refreshVolume();
  layoutStatus();
}

}  // namespace

bool deckHeaderCreate(lv_obj_t* parent) {
  if (!parent) {
    return false;
  }
  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, BOARD_LCD_H_RES, kHeaderH);
  lv_obj_set_pos(s_root, 0, 0);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_root, 0, 0);
  lv_obj_set_style_pad_all(s_root, 0, 0);
  lv_obj_set_style_radius(s_root, 0, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

  s_title = lv_label_create(s_root);
  const DeviceSettings settings = settingsStore.load();
  const char* device_name =
      settings.device_name.length() > 0 ? settings.device_name.c_str() : APP_DEFAULT_DEVICE_NAME;
  lv_label_set_text(s_title, device_name);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(s_title, lv_color_hex(0xE2E8F0), 0);
  lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 16, 0);

  s_wifi = lv_label_create(s_root);
  lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_20, 0);

  s_ws = lv_img_create(s_root);
  lv_img_set_src(s_ws, &icon_link_arrows);
  lv_obj_set_style_img_recolor_opa(s_ws, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(s_ws, lv_color_hex(0x64748B), 0);
  lv_obj_clear_flag(s_ws, LV_OBJ_FLAG_CLICKABLE);

  s_ble = lv_label_create(s_root);
  lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(s_ble, &lv_font_montserrat_20, 0);

  s_volume = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_volume, &lv_font_montserrat_20, 0);

  s_clock = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_20, 0);

  refreshConn();
  return true;
}

void deckHeaderTick() {
  if (!s_root) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_ms < 500 && !s_volume_dirty) {
    return;
  }
  s_last_ms = now;
  if (!displayDriverLock(40)) {
    return;
  }
  if (s_volume_dirty) {
    refreshVolume();
    layoutStatus();
    s_volume_dirty = false;
  }
  refreshConn();
  displayDriverUnlock();
}

void deckHeaderSetVolume(int volume, bool muted) {
  s_volume_level = constrain(volume, APP_VOLUME_MIN, APP_VOLUME_MAX);
  s_muted = muted;
  s_volume_dirty = true;
  if (!displayDriverLock(120)) {
    return;
  }
  refreshVolume();
  layoutStatus();
  s_volume_dirty = false;
  displayDriverUnlock();
}

void deckHeaderRaise() {
  if (s_root) {
    lv_obj_move_foreground(s_root);
  }
}

lv_coord_t deckHeaderHeight() {
  return kHeaderH;
}

lv_obj_t* deckHeaderRoot() {
  return s_root;
}
