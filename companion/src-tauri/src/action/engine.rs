use crate::action::{ActionKind, ActionRecord, VolumeState};
use crate::platform;
use crate::virtual_keyboard;

pub struct ActionEngine;

impl ActionEngine {
  pub fn new() -> Self {
    Self
  }

  /// Returns Some(volume) when the board UI should be synced.
  pub async fn execute(&self, action: &ActionRecord) -> Result<Option<VolumeState>, String> {
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
        if let Err(e) = virtual_keyboard::media(&action.value) {
          tracing::warn!("virtual keyboard media failed ({e}), falling back");
          platform::media_key(&action.value)?;
        }
        Ok(None)
      }
      ActionKind::Volume => {
        let op = if action.value == "volume_down" || action.value == "down" {
          "volume_down"
        } else {
          "volume_up"
        };
        if virtual_keyboard::media(op).is_ok() {
          let vol = platform::read_volume().unwrap_or(VolumeState {
            level: 50,
            muted: false,
          });
          return Ok(Some(vol));
        }
        let delta: i32 = if op == "volume_down" { -3 } else { 3 };
        Ok(Some(platform::adjust_volume(delta)?))
      }
      ActionKind::Mute => {
        if virtual_keyboard::media("mute").is_ok() {
          let vol = platform::read_volume().unwrap_or(VolumeState {
            level: 50,
            muted: true,
          });
          return Ok(Some(vol));
        }
        Ok(Some(platform::toggle_mute()?))
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
}
