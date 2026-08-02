#include "home_grid_screen.h"

#include <string.h>
#include <time.h>

#include "app_config.h"
#include "board_config.h"
#include "app/input_queue.h"
#include "display/display_driver.h"
#include "storage/profile_store.h"
#include "storage/icon_store.h"
#include "storage/settings_store.h"
#include "system/system_status.h"
#include "system/idle_manager.h"
#include "ble/ble_manager.h"
#include "ui/icons/icon_link_arrows.h"

namespace {

struct IconDef {
  const char* id;
  const char* glyph;
  bool wide;  // multi-character monograms need the smaller font
};

// Media entries use LVGL's built-in symbol glyphs; app entries use monograms.
const IconDef kIcons[] = {
    {"vol_up", LV_SYMBOL_VOLUME_MAX, false},
    {"vol_down", LV_SYMBOL_VOLUME_MID, false},
    {"mute", LV_SYMBOL_MUTE, false},
    {"play", LV_SYMBOL_PLAY, false},
    {"pause", LV_SYMBOL_PAUSE, false},
    {"next", LV_SYMBOL_NEXT, false},
    {"prev", LV_SYMBOL_PREV, false},
    {"shuffle", LV_SYMBOL_SHUFFLE, false},
    {"power", LV_SYMBOL_POWER, false},
    {"settings", LV_SYMBOL_SETTINGS, false},
    {"home", LV_SYMBOL_HOME, false},
    {"bell", LV_SYMBOL_BELL, false},
    {"mail", LV_SYMBOL_ENVELOPE, false},
    {"wifi", LV_SYMBOL_WIFI, false},
    {"file", LV_SYMBOL_FILE, false},
    {"folder", LV_SYMBOL_DIRECTORY, false},
    {"gpt", "G", false},
    {"codex", "CX", true},
    {"cursor", "C", false},
    {"iterm", ">_", true},
    {"terminal", ">_", true},
    {"vscode", "VS", true},
    {"slack", "S", false},
    {"telegram", "Tg", true},
    {"safari", "Sf", true},
    {"chrome", "Ch", true},
    {"finder", "F", false},
    {"music", LV_SYMBOL_AUDIO, false},
    {"messages", LV_SYMBOL_CALL, false},
    {"app", LV_SYMBOL_DRIVE, false},
};

lv_obj_t* s_root = nullptr;
lv_obj_t* s_tiles = nullptr;
lv_obj_t* s_status = nullptr;
lv_obj_t* s_wifi = nullptr;
lv_obj_t* s_ble = nullptr;
lv_obj_t* s_ws = nullptr;
lv_obj_t* s_volume_label = nullptr;
lv_obj_t* s_clock_label = nullptr;
GridConfig s_cfg{};
int s_volume = APP_VOLUME_DEFAULT;
bool s_muted = false;
uint32_t s_last_conn_ms = 0;
bool s_cursor_approval = false;
bool s_codex_approval = false;

// Host-routed tiles need an active GATT companion (no BLE HID / WS launch).
bool requiresCompanion(TileAction action) {
  return action == TileAction::App || action == TileAction::Mute || action == TileAction::PlayPause ||
         action == TileAction::Next || action == TileAction::Previous || action == TileAction::VolumeUp ||
         action == TileAction::VolumeDown;
}

bool tileAvailable(TileAction action) {
  if (requiresCompanion(action)) {
    return bleManagerIsConnected();
  }
  return true;
}

void updateTileAvailability() {
  if (!s_tiles) return;
  const uint32_t child_count = lv_obj_get_child_cnt(s_tiles);
  for (uint8_t i = 0; i < s_cfg.tile_count && i < child_count; ++i) {
    lv_obj_t* btn = lv_obj_get_child(s_tiles, i);
    if (tileAvailable(s_cfg.tiles[i].action)) {
      lv_obj_clear_state(btn, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
    }

    bool highlight = false;
    if (strcmp(s_cfg.tiles[i].icon, "cursor") == 0) {
      highlight = s_cursor_approval;
    } else if (strcmp(s_cfg.tiles[i].icon, "codex") == 0) {
      highlight = s_codex_approval;
    }
    if (highlight) {
      lv_obj_set_style_border_width(btn, 3, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFBBF24), 0);
    } else {
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x27354D), 0);
    }
  }
}

const IconDef* findIcon(const char* icon) {
  if (icon && icon[0]) {
    for (const IconDef& def : kIcons) {
      if (strcmp(def.id, icon) == 0) {
        return &def;
      }
    }
  }
  return &kIcons[sizeof(kIcons) / sizeof(kIcons[0]) - 1];
}

void layoutHeaderStatus() {
  // Right cluster: clock · volume · BT · WS · Wi-Fi
  if (s_wifi) {
    lv_obj_align(s_wifi, LV_ALIGN_TOP_RIGHT, -16, 16);
  }
  if (s_ws && s_wifi) {
    lv_obj_align_to(s_ws, s_wifi, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  }
  if (s_ble && s_ws) {
    lv_obj_align_to(s_ble, s_ws, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  }
  if (s_volume_label && s_ble) {
    lv_obj_align_to(s_volume_label, s_ble, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  }
  if (s_clock_label && s_volume_label) {
    lv_obj_align_to(s_clock_label, s_volume_label, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  }
}

void updateHeaderClock() {
  if (!s_clock_label) {
    return;
  }
  time_t now = time(nullptr);
  struct tm local {};
  if (now < 1700000000 || !localtime_r(&now, &local)) {
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0x64748B), 0);
    return;
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &local);
  lv_label_set_text(s_clock_label, buf);
  lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xE2E8F0), 0);
}

void updateConnStatus() {
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
    if (!bleManagerIsStarted()) {
      lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH " off");
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x475569), 0);
    } else if (bleManagerIsConnected()) {
      lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH);
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x4ADE80), 0);
    } else if (bleManagerPairMode()) {
      lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH " pair");
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0xFBBF24), 0);
    } else {
      lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH " idle");
      lv_obj_set_style_text_color(s_ble, lv_color_hex(0x64748B), 0);
    }
  }

  if (s_ws) {
    // Link icon now mirrors GATT companion link (WS launch removed in v4).
    lv_obj_set_style_img_recolor(
        s_ws, bleManagerIsConnected() ? lv_color_hex(0x4ADE80) : lv_color_hex(0x64748B), 0);
  }

  updateHeaderClock();
  updateTileAvailability();
  layoutHeaderStatus();
}

void updateStatus() {
  if (s_volume_label) {
    if (s_muted) {
      lv_label_set_text(s_volume_label, LV_SYMBOL_MUTE "  Muted");
      lv_obj_set_style_text_color(s_volume_label, lv_color_hex(0xF87171), 0);
    } else {
      lv_label_set_text_fmt(s_volume_label, LV_SYMBOL_VOLUME_MAX "  %d%%", s_volume);
      lv_obj_set_style_text_color(s_volume_label, lv_color_hex(0xE2E8F0), 0);
    }
  }
  updateHeaderClock();
  updateConnStatus();
  if (s_status) {
    lv_label_set_text(s_status, "edit: touchdeck.local/grid");
  }
}

void onTilePressed(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
    return;
  }
  if (!idleManagerAllowTilePress()) {
    return;
  }
  const uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (idx >= s_cfg.tile_count) {
    return;
  }
  const GridTile& tile = s_cfg.tiles[idx];
  if (!tileAvailable(tile.action)) {
    Serial.printf("[UI] tile %s disabled — companion not connected\n", tile.id);
    return;
  }
  Serial.printf("[UI] tile %s action=%s\n", tile.id, tileActionToString(tile.action));

  switch (tile.action) {
    case TileAction::VolumeUp:
      s_muted = false;
      s_volume = min(APP_VOLUME_MAX, s_volume + APP_VOLUME_STEP);
      updateStatus();
      break;
    case TileAction::VolumeDown:
      s_muted = false;
      s_volume = max(APP_VOLUME_MIN, s_volume - APP_VOLUME_STEP);
      updateStatus();
      break;
    case TileAction::Mute:
      s_muted = !s_muted;
      updateStatus();
      break;
    default:
      break;
  }
  inputQueue.pushTile(tile);
}

void clearTiles() {
  if (!s_tiles) {
    return;
  }
  while (lv_obj_get_child_cnt(s_tiles) > 0) {
    lv_obj_del(lv_obj_get_child(s_tiles, 0));
  }
}

bool buildTilesLocked() {
  if (!s_root || !s_tiles) {
    return false;
  }
  clearTiles();

  const lv_coord_t margin = 16;
  const lv_coord_t gap = 12;
  const lv_coord_t header = 56;
  const lv_coord_t footer = 40;
  const lv_coord_t avail_w = BOARD_LCD_H_RES - (margin * 2) - (gap * (s_cfg.cols - 1));
  const lv_coord_t avail_h = BOARD_LCD_V_RES - header - footer - (margin * 2) - (gap * (s_cfg.rows - 1));
  const lv_coord_t tw = avail_w / s_cfg.cols;
  const lv_coord_t th = avail_h / s_cfg.rows;
  const lv_coord_t badge = (th < tw ? th : tw) / 2;

  lv_obj_set_size(s_tiles, BOARD_LCD_H_RES, BOARD_LCD_V_RES - header - footer);
  lv_obj_set_pos(s_tiles, 0, header);

  for (uint8_t i = 0; i < s_cfg.tile_count; ++i) {
    const uint8_t col = i % s_cfg.cols;
    const uint8_t row = i / s_cfg.cols;
    const GridTile& tile = s_cfg.tiles[i];
    const lv_img_dsc_t* custom = iconStoreGet(tile.icon);
    const IconDef* icon = findIcon(tile.icon);

    lv_obj_t* btn = lv_btn_create(s_tiles);
    lv_obj_set_size(btn, tw, th);
    lv_obj_set_pos(btn, margin + col * (tw + gap), margin + row * (th + gap));
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x161F32), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B3A55), LV_STATE_PRESSED);
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x27354D), 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_PRESSED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

    if (custom) {
      lv_obj_t* img = lv_img_create(btn);
      lv_img_set_src(img, custom);
      lv_obj_align(img, LV_ALIGN_CENTER, 0, -16);
      lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_t* circle = lv_obj_create(btn);
      lv_obj_set_size(circle, badge, badge);
      lv_obj_align(circle, LV_ALIGN_CENTER, 0, -16);
      lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(circle, lv_color_hex(tile.color), 0);
      lv_obj_set_style_border_width(circle, 0, 0);
      lv_obj_set_style_pad_all(circle, 0, 0);
      lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t* glyph = lv_label_create(circle);
      lv_label_set_text(glyph, icon->glyph);
      lv_obj_set_style_text_font(glyph, icon->wide ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);
      lv_obj_set_style_text_color(glyph, lv_color_hex(0xFFFFFF), 0);
      lv_obj_center(glyph);
    }

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, tile.label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
  }

  updateStatus();
  return true;
}

}  // namespace

bool homeGridScreenCreate() {
  s_cfg = profileStore.current();
  char err[64];
  if (!gridConfigValidate(s_cfg, err, sizeof(err))) {
    gridConfigSetDefaults(s_cfg);
  }

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  s_root = scr;

  lv_obj_t* title = lv_label_create(scr);
  const DeviceSettings settings = settingsStore.load();
  const char* device_name =
      settings.device_name.length() > 0 ? settings.device_name.c_str() : APP_DEFAULT_DEVICE_NAME;
  lv_label_set_text(title, device_name);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

  s_wifi = lv_label_create(scr);
  lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_wifi, lv_color_hex(0x38BDF8), 0);

  s_ws = lv_img_create(scr);
  lv_img_set_src(s_ws, &icon_link_arrows);
  lv_obj_set_style_img_recolor_opa(s_ws, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(s_ws, lv_color_hex(0x64748B), 0);
  lv_obj_clear_flag(s_ws, LV_OBJ_FLAG_CLICKABLE);

  s_ble = lv_label_create(scr);
  lv_label_set_text(s_ble, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(s_ble, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_ble, lv_color_hex(0x64748B), 0);

  s_volume_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_volume_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_volume_label, lv_color_hex(0xE2E8F0), 0);

  s_clock_label = lv_label_create(scr);
  lv_label_set_text(s_clock_label, "--:--");
  lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0x64748B), 0);

  s_tiles = lv_obj_create(scr);
  lv_obj_set_style_bg_opa(s_tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_tiles, 0, 0);
  lv_obj_set_style_pad_all(s_tiles, 0, 0);
  lv_obj_clear_flag(s_tiles, LV_OBJ_FLAG_SCROLLABLE);

  s_status = lv_label_create(scr);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -12);

  if (!buildTilesLocked()) {
    return false;
  }
  Serial.printf("[UI] Home grid %ux%u ready\n", s_cfg.cols, s_cfg.rows);
  return true;
}

bool homeGridScreenReload(const GridConfig& cfg) {
  char err[64];
  if (!gridConfigValidate(cfg, err, sizeof(err))) {
    Serial.printf("[UI] reload reject: %s\n", err);
    return false;
  }
  if (!displayDriverLock(500)) {
    Serial.println("[UI] reload lock timeout");
    return false;
  }
  s_cfg = cfg;
  const bool ok = buildTilesLocked();
  displayDriverUnlock();
  if (ok) {
    Serial.printf("[UI] Grid reloaded rev=%u\n", s_cfg.rev);
  }
  return ok;
}

void homeGridScreenSetVolume(int volume, bool muted) {
  s_volume = constrain(volume, APP_VOLUME_MIN, APP_VOLUME_MAX);
  s_muted = muted;
  if (displayDriverLock(50)) {
    updateStatus();
    displayDriverUnlock();
  }
}

int homeGridScreenGetVolume() { return s_volume; }

bool homeGridScreenIsMuted() { return s_muted; }

void homeGridScreenSetApprovalHighlight(const char* source, bool on) {
  if (!source || !source[0]) {
    return;
  }
  if (strcmp(source, "cursor") == 0) {
    s_cursor_approval = on;
  } else if (strcmp(source, "codex") == 0) {
    s_codex_approval = on;
  } else {
    return;
  }
  if (displayDriverLock(50)) {
    updateTileAvailability();
    displayDriverUnlock();
  }
}

void homeGridScreenClearApprovalHighlights() {
  s_cursor_approval = false;
  s_codex_approval = false;
  if (displayDriverLock(50)) {
    updateTileAvailability();
    displayDriverUnlock();
  }
}

void homeGridScreenTick() {
  if (!s_root) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_conn_ms < 500) {
    return;
  }
  s_last_conn_ms = now;
  if (displayDriverLock(30)) {
    updateConnStatus();
    displayDriverUnlock();
  }
}

lv_obj_t* homeGridScreenRoot() { return s_root; }
