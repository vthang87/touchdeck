#include "grid_config.h"

#include <Arduino.h>
#include <string.h>

const char* tileActionToString(TileAction action) {
  switch (action) {
    case TileAction::App:
      return "app";
    case TileAction::VolumeUp:
      return "volume_up";
    case TileAction::VolumeDown:
      return "volume_down";
    case TileAction::Mute:
      return "mute";
    case TileAction::PlayPause:
      return "play_pause";
    case TileAction::Next:
      return "next";
    case TileAction::Previous:
      return "previous";
    default:
      return "none";
  }
}

TileAction tileActionFromString(const char* s) {
  if (!s) {
    return TileAction::None;
  }
  if (strcmp(s, "app") == 0) return TileAction::App;
  if (strcmp(s, "volume_up") == 0) return TileAction::VolumeUp;
  if (strcmp(s, "volume_down") == 0) return TileAction::VolumeDown;
  if (strcmp(s, "mute") == 0) return TileAction::Mute;
  if (strcmp(s, "play_pause") == 0) return TileAction::PlayPause;
  if (strcmp(s, "next") == 0) return TileAction::Next;
  if (strcmp(s, "previous") == 0) return TileAction::Previous;
  return TileAction::None;
}

const char* appTargetKindToString(AppTargetKind kind) {
  switch (kind) {
    case AppTargetKind::Bundle:
      return "bundle";
    case AppTargetKind::Path:
      return "path";
    default:
      return "none";
  }
}

AppTargetKind appTargetKindFromString(const char* s) {
  if (!s) {
    return AppTargetKind::None;
  }
  if (strcmp(s, "bundle") == 0) return AppTargetKind::Bundle;
  if (strcmp(s, "path") == 0) return AppTargetKind::Path;
  return AppTargetKind::None;
}

void gridConfigEnsureActionId(GridTile& tile) {
  if (tile.action_id[0] != '\0') {
    return;
  }
  if (tile.action == TileAction::App) {
    snprintf(tile.action_id, sizeof(tile.action_id), "open_%s", tile.id);
  } else {
    strncpy(tile.action_id, tileActionToString(tile.action), GRID_ACTION_ID_MAX);
  }
}

static void setTile(GridTile& t, const char* id, const char* label, uint32_t color, const char* icon,
                    TileAction action, const char* action_id, AppTargetKind kind = AppTargetKind::None,
                    const char* value = "") {
  memset(&t, 0, sizeof(t));
  strncpy(t.id, id, GRID_ID_MAX);
  strncpy(t.label, label, GRID_LABEL_MAX);
  t.color = color;
  strncpy(t.icon, icon, GRID_ICON_MAX);
  t.action = action;
  if (action_id && action_id[0]) {
    strncpy(t.action_id, action_id, GRID_ACTION_ID_MAX);
  } else {
    gridConfigEnsureActionId(t);
  }
  t.app_kind = kind;
  if (value) {
    strncpy(t.app_value, value, GRID_APP_VALUE_MAX);
  }
}

void gridConfigSetDefaults(GridConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.cols = 4;
  cfg.rows = 2;
  cfg.rev = 1;
  cfg.tile_count = 8;
  setTile(cfg.tiles[0], "vol_down", "Vol -", 0x475569, "vol_down", TileAction::VolumeDown, "volume_down");
  setTile(cfg.tiles[1], "mute", "Mute", 0x64748B, "mute", TileAction::Mute, "mute");
  setTile(cfg.tiles[2], "vol_up", "Vol +", 0x475569, "vol_up", TileAction::VolumeUp, "volume_up");
  setTile(cfg.tiles[3], "telegram", "Telegram", 0x229ED9, "telegram", TileAction::App, "open_telegram",
          AppTargetKind::Bundle, "ru.keepcoder.Telegram");
  setTile(cfg.tiles[4], "gpt", "ChatGPT", 0x10A37F, "gpt", TileAction::App, "open_chatgpt", AppTargetKind::Bundle,
          "com.openai.chat");
  setTile(cfg.tiles[5], "codex", "Codex", 0x0D8A6A, "codex", TileAction::App, "open_codex", AppTargetKind::Bundle,
          "com.openai.codex");
  setTile(cfg.tiles[6], "cursor", "Cursor", 0x6366F1, "cursor", TileAction::App, "open_cursor",
          AppTargetKind::Bundle, "com.todesktop.230313mzl4w4u92");
  setTile(cfg.tiles[7], "iterm", "iTerm", 0x1F2937, "iterm", TileAction::App, "open_iterm", AppTargetKind::Bundle,
          "com.googlecode.iterm2");
}

bool gridConfigValidate(const GridConfig& cfg, char* err, size_t err_len) {
  auto fail = [&](const char* msg) -> bool {
    if (err && err_len) {
      strncpy(err, msg, err_len - 1);
      err[err_len - 1] = '\0';
    }
    return false;
  };

  if (cfg.cols < GRID_MIN_COLS || cfg.cols > GRID_MAX_COLS) {
    return fail("cols out of range");
  }
  if (cfg.rows < GRID_MIN_ROWS || cfg.rows > GRID_MAX_ROWS) {
    return fail("rows out of range");
  }
  const uint8_t expected = static_cast<uint8_t>(cfg.cols * cfg.rows);
  if (cfg.tile_count != expected || cfg.tile_count > GRID_MAX_TILES) {
    return fail("tile_count mismatch");
  }
  for (uint8_t i = 0; i < cfg.tile_count; ++i) {
    const GridTile& t = cfg.tiles[i];
    if (t.id[0] == '\0') {
      return fail("tile id empty");
    }
    if (t.action == TileAction::None) {
      return fail("tile action invalid");
    }
    if (t.action_id[0] == '\0') {
      return fail("tile action_id empty");
    }
    if (strpbrk(t.action_id, ";&|`$<>\n\r\"'\\") != nullptr) {
      return fail("action_id has forbidden characters");
    }
    if (t.action == TileAction::App && t.app_value[0] != '\0') {
      if (strpbrk(t.app_value, ";&|`$<>\n\r") != nullptr) {
        return fail("app value has forbidden characters");
      }
    }
  }
  return true;
}
