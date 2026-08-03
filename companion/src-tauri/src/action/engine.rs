use crate::action::{ActionKind, ActionRecord, VolumeState};
use crate::platform;
use crate::virtual_keyboard;

pub struct ActionEngine;

impl ActionEngine {
  pub fn new() -> Self {
    Self
  }

  /// Synchronous action execution (safe to call from `spawn_blocking`).
  pub fn execute_blocking(&self, action: &ActionRecord) -> Result<Option<VolumeState>, String> {
    match action.kind {
      ActionKind::OpenApp => {
        platform::open_app(&action.value)?;
        Ok(None)
      }
      ActionKind::OpenUrl => {
        platform::open_url(&action.value)?;
        Ok(None)
      }
      ActionKind::Media => {
        let op = action.value.as_str();
        if op == "seek_fwd"
          || op == "media_seek_fwd"
          || op == "seek_back"
          || op == "media_seek_back"
          || op == "rate_up"
          || op == "media_rate_up"
          || op == "rate_down"
          || op == "media_rate_down"
          || op == "rate_1x"
          || op == "media_rate_1x"
        {
          platform::media_key(op)?;
          return Ok(None);
        }
        if let Err(e) = virtual_keyboard::media(op) {
          tracing::warn!("virtual keyboard media failed ({e}), falling back");
          platform::media_key(op)?;
        }
        Ok(None)
      }
      ActionKind::Volume => {
        let op = if action.value == "volume_down" || action.value == "down" {
          "volume_down"
        } else {
          "volume_up"
        };
        let delta: i32 = if op == "volume_down" { -3 } else { 3 };
        // Prefer osascript ±3 so readback matches the board step. HID VolumeUp/Down
        // often returns the *previous* level if we read immediately after the key.
        if let Ok(vol) = platform::adjust_volume(delta) {
          return Ok(Some(vol));
        }
        virtual_keyboard::media(op)?;
        std::thread::sleep(std::time::Duration::from_millis(100));
        Ok(Some(platform::read_volume().unwrap_or(VolumeState {
          level: 50,
          muted: false,
        })))
      }
      ActionKind::Mute => {
        if let Ok(vol) = platform::toggle_mute() {
          return Ok(Some(vol));
        }
        virtual_keyboard::media("mute")?;
        std::thread::sleep(std::time::Duration::from_millis(100));
        Ok(Some(platform::read_volume().unwrap_or(VolumeState {
          level: 50,
          muted: true,
        })))
      }
      ActionKind::Keyboard => {
        if let Err(e) = virtual_keyboard::keyboard(&action.value) {
          tracing::warn!("virtual keyboard failed ({e}), falling back");
          platform::keyboard(&action.value)?;
        }
        Ok(None)
      }
    }
  }

  /// Returns Some(volume) when the board UI should be synced.
  pub async fn execute(&self, action: &ActionRecord) -> Result<Option<VolumeState>, String> {
    self.execute_blocking(action)
  }
}
