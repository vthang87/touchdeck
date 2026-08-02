#include "input_queue.h"

#include <string.h>

InputQueue inputQueue;

void InputQueue::begin() {
  head_ = tail_ = count_ = 0;
}

bool InputQueue::push(InputAction action) {
  portENTER_CRITICAL(&mux_);
  if (count_ >= kCapacity) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  InputEvent& slot = buf_[tail_];
  memset(&slot, 0, sizeof(slot));
  slot.action = action;
  slot.timestamp_ms = millis();
  tail_ = (tail_ + 1) % kCapacity;
  ++count_;
  portEXIT_CRITICAL(&mux_);
  return true;
}

bool InputQueue::pushTile(const GridTile& tile) {
  InputAction action = InputAction::None;
  switch (tile.action) {
    case TileAction::VolumeUp:
      action = InputAction::VolumeUp;
      break;
    case TileAction::VolumeDown:
      action = InputAction::VolumeDown;
      break;
    case TileAction::Mute:
      action = InputAction::MuteToggle;
      break;
    case TileAction::PlayPause:
      action = InputAction::PlayPause;
      break;
    case TileAction::Next:
      action = InputAction::NextTrack;
      break;
    case TileAction::Previous:
      action = InputAction::PrevTrack;
      break;
    case TileAction::App:
      action = InputAction::LaunchApp;
      break;
    default:
      return false;
  }

  portENTER_CRITICAL(&mux_);
  if (count_ >= kCapacity) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  InputEvent& slot = buf_[tail_];
  memset(&slot, 0, sizeof(slot));
  slot.action = action;
  slot.timestamp_ms = millis();
  strncpy(slot.tile_id, tile.id, GRID_ID_MAX);
  if (tile.action_id[0]) {
    strncpy(slot.action_id, tile.action_id, GRID_ACTION_ID_MAX);
  } else {
    GridTile tmp = tile;
    gridConfigEnsureActionId(tmp);
    strncpy(slot.action_id, tmp.action_id, GRID_ACTION_ID_MAX);
  }
  if (action == InputAction::LaunchApp) {
    slot.app_kind = tile.app_kind;
    strncpy(slot.app_value, tile.app_value, GRID_APP_VALUE_MAX);
  }
  tail_ = (tail_ + 1) % kCapacity;
  ++count_;
  portEXIT_CRITICAL(&mux_);
  return true;
}

bool InputQueue::pop(InputEvent& out) {
  portENTER_CRITICAL(&mux_);
  if (count_ == 0) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  out = buf_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  portEXIT_CRITICAL(&mux_);
  return true;
}
