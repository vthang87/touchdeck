#pragma once

#include <stdint.h>
#include <stddef.h>

#define GRID_MIN_COLS 2
#define GRID_MAX_COLS 5
#define GRID_MIN_ROWS 1
#define GRID_MAX_ROWS 3
#define GRID_MAX_TILES (GRID_MAX_COLS * GRID_MAX_ROWS)

#define GRID_ID_MAX 23
#define GRID_LABEL_MAX 23
#define GRID_ICON_MAX 15
#define GRID_APP_VALUE_MAX 95
#define GRID_ACTION_ID_MAX 32

#define GRID_JSON_MAX_BYTES 4096
#define GRID_FILE_PATH "/grid.json"
#define GRID_TMP_PATH "/grid.json.tmp"

enum class TileAction : uint8_t {
  None = 0,
  App,
  VolumeUp,
  VolumeDown,
  Mute,
  PlayPause,
  Next,
  Previous,
};

enum class AppTargetKind : uint8_t {
  None = 0,
  Bundle,
  Path,
};

struct GridTile {
  char id[GRID_ID_MAX + 1];
  char label[GRID_LABEL_MAX + 1];
  uint32_t color;  // 0xRRGGBB
  char icon[GRID_ICON_MAX + 1];
  TileAction action;
  char action_id[GRID_ACTION_ID_MAX + 1];
  AppTargetKind app_kind;
  char app_value[GRID_APP_VALUE_MAX + 1];
};

struct GridConfig {
  uint8_t cols;
  uint8_t rows;
  uint16_t rev;
  uint8_t tile_count;
  GridTile tiles[GRID_MAX_TILES];
};

const char* tileActionToString(TileAction action);
TileAction tileActionFromString(const char* s);
const char* appTargetKindToString(AppTargetKind kind);
AppTargetKind appTargetKindFromString(const char* s);

void gridConfigSetDefaults(GridConfig& cfg);
bool gridConfigValidate(const GridConfig& cfg, char* err, size_t err_len);
void gridConfigEnsureActionId(GridTile& tile);
