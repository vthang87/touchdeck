#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "windows")]
mod windows;

use crate::action::VolumeState;

#[cfg(target_os = "macos")]
pub use macos::*;

#[cfg(target_os = "windows")]
pub use windows::*;

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn open_app(_target: &str) -> Result<(), String> {
  Err("open_app not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn open_url(_url: &str) -> Result<(), String> {
  Err("open_url not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn adjust_volume(_delta: i32) -> Result<VolumeState, String> {
  Err("volume not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn toggle_mute() -> Result<VolumeState, String> {
  Err("mute not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn read_volume() -> Result<VolumeState, String> {
  Err("volume not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn media_key(_op: &str) -> Result<(), String> {
  Err("media not supported on this OS".into())
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn keyboard(_spec: &str) -> Result<(), String> {
  Err("keyboard not supported on this OS".into())
}

pub fn sanitize_bundle(value: &str) -> Result<&str, String> {
  if value.is_empty() || value.len() > 200 {
    return Err("invalid target length".into());
  }
  if value.chars().any(|c| !c.is_ascii_alphanumeric() && c != '.' && c != '-') {
    return Err("bundle id has forbidden characters".into());
  }
  Ok(value)
}

pub fn sanitize_path(value: &str) -> Result<&str, String> {
  if !value.starts_with('/') || !value.ends_with(".app") || value.len() > 200 {
    return Err("path must be absolute .app".into());
  }
  if value.contains(|c: char| ";|&`$<>\n\r".contains(c)) {
    return Err("path has forbidden characters".into());
  }
  Ok(value)
}

#[allow(dead_code)]
fn _vol_placeholder() -> VolumeState {
  VolumeState {
    level: 50,
    muted: false,
  }
}
