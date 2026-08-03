#include "home_grid_screen.h"

#include <string.h>

#include "app_config.h"
#include "board_config.h"
#include "app/input_queue.h"
#include "display/display_driver.h"
#include "storage/profile_store.h"
#include "storage/icon_store.h"
#include "system/idle_manager.h"
#include "ble/ble_manager.h"
#include "ui/screens/deck_header.h"
#include "ui/screens/workspace_pager.h"

namespace {

struct IconDef {
  const char* id;
  const char* glyph;
  bool wide;
};

const IconDef kIcons[] = {
    {"vol_up", LV_SYMBOL_VOLUME_MAX, false},
    {"vol_down", LV_SYMBOL_VOLUME_MID, false},
    {"mute", LV_SYMBOL_VOLUME_MID, false},
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

struct GridPage {
  lv_obj_t* root = nullptr;
  lv_obj_t* tiles = nullptr;
  GridConfig cfg{};
};

GridPage s_pages[DECK_SHORTCUT_PAGES_MAX];
uint8_t s_mounted = 0;
int s_volume = APP_VOLUME_DEFAULT;
bool s_muted = false;
bool s_cursor_approval = false;
bool s_codex_approval = false;
uint32_t s_last_tile_ms = 0;
uint32_t s_last_avail_ms = 0;
char s_last_tile_id[GRID_ID_MAX + 1] = {};
constexpr uint32_t kTileDebounceMs = 180;

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

void updateTileAvailability(GridPage& page) {
  if (!page.tiles) return;
  const uint32_t child_count = lv_obj_get_child_cnt(page.tiles);
  for (uint8_t i = 0; i < page.cfg.tile_count && i < child_count; ++i) {
    lv_obj_t* btn = lv_obj_get_child(page.tiles, i);
    if (tileAvailable(page.cfg.tiles[i].action)) {
      lv_obj_clear_state(btn, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
    }

    bool highlight = false;
    if (strcmp(page.cfg.tiles[i].icon, "cursor") == 0) {
      highlight = s_cursor_approval;
    } else if (strcmp(page.cfg.tiles[i].icon, "codex") == 0) {
      highlight = s_codex_approval;
    }

    const bool is_mute = page.cfg.tiles[i].action == TileAction::Mute;
    if (is_mute && s_muted) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x3F1212), 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xF87171), 0);
      lv_obj_t* badge = lv_obj_get_child(btn, 0);
      if (badge && !lv_obj_check_type(badge, &lv_img_class)) {
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x7F1D1D), 0);
        lv_obj_t* glyph = lv_obj_get_child(badge, 0);
        if (glyph) {
          lv_label_set_text(glyph, LV_SYMBOL_MUTE);
          lv_obj_set_style_text_color(glyph, lv_color_hex(0xFCA5A5), 0);
        }
      }
    } else if (is_mute) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x161F32), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x27354D), 0);
      lv_obj_t* badge = lv_obj_get_child(btn, 0);
      if (badge && !lv_obj_check_type(badge, &lv_img_class)) {
        lv_obj_set_style_bg_color(badge, lv_color_hex(page.cfg.tiles[i].color), 0);
        lv_obj_t* glyph = lv_obj_get_child(badge, 0);
        if (glyph) {
          lv_label_set_text(glyph, LV_SYMBOL_VOLUME_MID);
          lv_obj_set_style_text_color(glyph, lv_color_hex(0xFFFFFF), 0);
        }
      }
    } else if (highlight) {
      lv_obj_set_style_border_width(btn, 3, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFBBF24), 0);
    } else {
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x27354D), 0);
    }
  }
}

void refreshAllMuteTiles() {
  for (uint8_t p = 0; p < s_mounted; ++p) {
    updateTileAvailability(s_pages[p]);
  }
}

void clearTiles(GridPage& page) {
  if (!page.tiles) {
    return;
  }
  while (lv_obj_get_child_cnt(page.tiles) > 0) {
    lv_obj_del(lv_obj_get_child(page.tiles, 0));
  }
}

void onTilePressed(lv_event_t* e) {
  if (workspacePagerTouchGate(e)) {
    return;
  }
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (!idleManagerAllowTilePress()) {
    return;
  }
  const uintptr_t packed = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  const uint8_t page_idx = static_cast<uint8_t>((packed >> 16) & 0xFF);
  const uint8_t tile_idx = static_cast<uint8_t>(packed & 0xFF);
  if (page_idx >= s_mounted) {
    return;
  }
  GridPage& page = s_pages[page_idx];
  if (tile_idx >= page.cfg.tile_count) {
    return;
  }
  const GridTile& tile = page.cfg.tiles[tile_idx];
  if (!tileAvailable(tile.action)) {
    Serial.printf("[UI] tile %s disabled — companion not connected\n", tile.id);
    return;
  }
  const uint32_t now = millis();
  if (s_last_tile_id[0] && strcmp(s_last_tile_id, tile.id) == 0 &&
      (now - s_last_tile_ms) < kTileDebounceMs) {
    return;
  }
  s_last_tile_ms = now;
  strncpy(s_last_tile_id, tile.id, GRID_ID_MAX);
  s_last_tile_id[GRID_ID_MAX] = '\0';

  Serial.printf("[UI] tile %s action=%s\n", tile.id, tileActionToString(tile.action));

  switch (tile.action) {
    case TileAction::VolumeUp:
      s_muted = false;
      s_volume = min(APP_VOLUME_MAX, s_volume + APP_VOLUME_STEP);
      deckHeaderSetVolume(s_volume, s_muted);
      refreshAllMuteTiles();
      break;
    case TileAction::VolumeDown:
      s_muted = false;
      s_volume = max(APP_VOLUME_MIN, s_volume - APP_VOLUME_STEP);
      deckHeaderSetVolume(s_volume, s_muted);
      refreshAllMuteTiles();
      break;
    case TileAction::Mute:
      s_muted = !s_muted;
      deckHeaderSetVolume(s_volume, s_muted);
      refreshAllMuteTiles();
      break;
    default:
      break;
  }
  inputQueue.pushTile(tile);
}

bool buildTilesLocked(GridPage& page, uint8_t page_idx) {
  if (!page.root || !page.tiles) {
    return false;
  }
  clearTiles(page);

  const lv_coord_t margin = 16;
  const lv_coord_t gap = 12;
  const lv_coord_t header = deckHeaderHeight();
  const lv_coord_t footer = 40;
  const lv_coord_t avail_w = BOARD_LCD_H_RES - (margin * 2) - (gap * (page.cfg.cols - 1));
  const lv_coord_t avail_h =
      BOARD_LCD_V_RES - header - footer - (margin * 2) - (gap * (page.cfg.rows - 1));
  const lv_coord_t tw = avail_w / page.cfg.cols;
  const lv_coord_t th = avail_h / page.cfg.rows;
  const lv_coord_t badge = (th < tw ? th : tw) / 2;

  lv_obj_set_size(page.tiles, BOARD_LCD_H_RES, BOARD_LCD_V_RES - header - footer);
  lv_obj_set_pos(page.tiles, 0, header);

  for (uint8_t i = 0; i < page.cfg.tile_count; ++i) {
    const uint8_t col = i % page.cfg.cols;
    const uint8_t row = i / page.cfg.cols;
    const GridTile& tile = page.cfg.tiles[i];
    const lv_img_dsc_t* custom = iconStoreGet(tile.icon);
    const IconDef* icon = findIcon(tile.icon);
    const uintptr_t packed =
        (static_cast<uintptr_t>(page_idx) << 16) | static_cast<uintptr_t>(i);

    lv_obj_t* btn = lv_btn_create(page.tiles);
    lv_obj_set_size(btn, tw, th);
    lv_obj_set_pos(btn, margin + col * (tw + gap), margin + row * (th + gap));
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x161F32), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B3A55), LV_STATE_PRESSED);
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x27354D), 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_PRESSED, reinterpret_cast<void*>(packed));
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_PRESSING, reinterpret_cast<void*>(packed));
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_RELEASED, reinterpret_cast<void*>(packed));
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_PRESS_LOST, reinterpret_cast<void*>(packed));
    lv_obj_add_event_cb(btn, onTilePressed, LV_EVENT_CLICKED, reinterpret_cast<void*>(packed));

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

  updateTileAvailability(page);
  return true;
}

}  // namespace

bool homeGridScreenCreate() {
  return homeGridScreenCreateOn(lv_scr_act());
}

bool homeGridScreenCreateAt(lv_obj_t* parent, uint8_t shortcut_index) {
  if (!parent || shortcut_index >= DECK_SHORTCUT_PAGES_MAX) {
    return false;
  }
  const DeckProfile& profile = profileStore.profile();
  GridConfig cfg = (shortcut_index < profile.shortcut_count) ? profile.pages[shortcut_index]
                                                             : GridConfig{};
  char err[64];
  if (!gridConfigValidate(cfg, err, sizeof(err))) {
    gridConfigSetDefaults(cfg);
  }

  GridPage& page = s_pages[shortcut_index];
  page = GridPage{};
  page.cfg = cfg;

  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  page.root = parent;

  page.tiles = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(page.tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(page.tiles, 0, 0);
  lv_obj_set_style_pad_all(page.tiles, 0, 0);
  lv_obj_clear_flag(page.tiles, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page.tiles, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (shortcut_index + 1 > s_mounted) {
    s_mounted = static_cast<uint8_t>(shortcut_index + 1);
  }

  if (!buildTilesLocked(page, shortcut_index)) {
    return false;
  }
  Serial.printf("[UI] Home grid page %u %ux%u ready\n", shortcut_index, page.cfg.cols, page.cfg.rows);
  return true;
}

bool homeGridScreenCreateOn(lv_obj_t* parent) {
  s_mounted = 0;
  return homeGridScreenCreateAt(parent, 0);
}

bool homeGridScreenReloadPageLocked(uint8_t shortcut_index, const GridConfig& cfg) {
  if (shortcut_index >= s_mounted) {
    return false;
  }
  char err[64];
  if (!gridConfigValidate(cfg, err, sizeof(err))) {
    Serial.printf("[UI] reload reject: %s\n", err);
    return false;
  }
  s_pages[shortcut_index].cfg = cfg;
  const bool ok = buildTilesLocked(s_pages[shortcut_index], shortcut_index);
  if (ok) {
    Serial.printf("[UI] Grid page %u reloaded rev=%u\n", shortcut_index, cfg.rev);
  }
  return ok;
}

bool homeGridScreenReloadLocked(const GridConfig& cfg) {
  return homeGridScreenReloadPageLocked(0, cfg);
}

bool homeGridScreenReload(const GridConfig& cfg) {
  if (!displayDriverLock(500)) {
    Serial.println("[UI] reload lock timeout");
    return false;
  }
  const bool ok = homeGridScreenReloadLocked(cfg);
  displayDriverUnlock();
  return ok;
}

void homeGridScreenSetVolume(int volume, bool muted) {
  s_volume = constrain(volume, APP_VOLUME_MIN, APP_VOLUME_MAX);
  s_muted = muted;
  deckHeaderSetVolume(s_volume, s_muted);
  if (displayDriverLock(80)) {
    refreshAllMuteTiles();
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
    for (uint8_t i = 0; i < s_mounted; ++i) {
      updateTileAvailability(s_pages[i]);
    }
    displayDriverUnlock();
  }
}

void homeGridScreenClearApprovalHighlights() {
  s_cursor_approval = false;
  s_codex_approval = false;
  if (displayDriverLock(50)) {
    for (uint8_t i = 0; i < s_mounted; ++i) {
      updateTileAvailability(s_pages[i]);
    }
    displayDriverUnlock();
  }
}

void homeGridScreenTick() {
  if (s_mounted == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_avail_ms < 500) {
    return;
  }
  s_last_avail_ms = now;
  if (displayDriverLock(30)) {
    for (uint8_t i = 0; i < s_mounted; ++i) {
      updateTileAvailability(s_pages[i]);
    }
    displayDriverUnlock();
  }
}

lv_obj_t* homeGridScreenRoot() {
  return s_mounted > 0 ? s_pages[0].root : nullptr;
}
