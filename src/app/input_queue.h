#pragma once

#include <Arduino.h>

#include "grid_config.h"

enum class InputAction : uint8_t {
  None = 0,
  VolumeUp,
  VolumeDown,
  MuteToggle,
  PlayPause,
  NextTrack,
  PrevTrack,
  LaunchApp,
  Restart,
  GetStatus,
};

struct InputEvent {
  InputAction action = InputAction::None;
  uint32_t timestamp_ms = 0;
  char tile_id[GRID_ID_MAX + 1] = {};
  AppTargetKind app_kind = AppTargetKind::None;
  char app_value[GRID_APP_VALUE_MAX + 1] = {};
};

class InputQueue {
 public:
  void begin();
  bool push(InputAction action);
  bool pushTile(const GridTile& tile);
  bool pop(InputEvent& out);

 private:
  static constexpr size_t kCapacity = 16;
  InputEvent buf_[kCapacity];
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern InputQueue inputQueue;
