use crate::action::VolumeState;

pub fn open_app(_target: &str) -> Result<(), String> {
  Err("Windows OpenApp scaffold — not implemented yet".into())
}

pub fn open_url(_url: &str) -> Result<(), String> {
  Err("Windows OpenUrl scaffold — not implemented yet".into())
}

pub fn adjust_volume(_delta: i32) -> Result<VolumeState, String> {
  Err("Windows volume scaffold — not implemented yet".into())
}

pub fn toggle_mute() -> Result<VolumeState, String> {
  Err("Windows mute scaffold — not implemented yet".into())
}

pub fn read_volume() -> Result<VolumeState, String> {
  Err("Windows volume scaffold — not implemented yet".into())
}

pub fn media_key(_op: &str) -> Result<(), String> {
  Err("Windows media scaffold — not implemented yet".into())
}

pub fn keyboard(_spec: &str) -> Result<(), String> {
  Err("Windows keyboard scaffold — not implemented yet".into())
}
