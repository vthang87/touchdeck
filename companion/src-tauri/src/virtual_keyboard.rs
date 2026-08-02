//! Virtual keyboard — inject keystrokes / media keys into the host OS.
//! Requires Accessibility on macOS.

use enigo::{
  Direction, Enigo, Key, Keyboard, Settings,
};

/// Press a named media / system key.
pub fn media(op: &str) -> Result<(), String> {
  // macOS fine volume: Option+Shift+Vol (¼-step).
  match op {
    "volume_up_fine" | "vol_up_fine" => return keyboard("opt+shift+volume_up"),
    "volume_down_fine" | "vol_down_fine" => return keyboard("opt+shift+volume_down"),
    _ => {}
  }
  let mut enigo = Enigo::new(&Settings::default()).map_err(|e| e.to_string())?;
  let key = match op {
    "play_pause" | "playpause" => Key::MediaPlayPause,
    "next" => Key::MediaNextTrack,
    "previous" | "prev" => Key::MediaPrevTrack,
    "volume_up" => Key::VolumeUp,
    "volume_down" => Key::VolumeDown,
    "mute" => Key::VolumeMute,
    _ => return Err(format!("unknown media key: {op}")),
  };
  enigo.key(key, Direction::Click).map_err(|e| e.to_string())?;
  Ok(())
}

/// Type text or fire a shortcut like `cmd+c`, `cmd+shift+p`.
pub fn keyboard(spec: &str) -> Result<(), String> {
  let spec = spec.trim();
  if spec.is_empty() {
    return Err("empty keyboard spec".into());
  }
  if spec.contains('+') {
    return shortcut(spec);
  }
  type_text(spec)
}

fn type_text(text: &str) -> Result<(), String> {
  if text.len() > 200 {
    return Err("text too long".into());
  }
  if text.chars().any(|c| ";|&`$<>".contains(c)) {
    return Err("text has forbidden characters".into());
  }
  let mut enigo = Enigo::new(&Settings::default()).map_err(|e| e.to_string())?;
  enigo.text(text).map_err(|e| e.to_string())?;
  Ok(())
}

fn shortcut(spec: &str) -> Result<(), String> {
  let lower = spec.to_lowercase();
  let parts: Vec<&str> = lower
    .split('+')
    .map(|s| s.trim())
    .filter(|s| !s.is_empty())
    .collect();
  if parts.is_empty() {
    return Err("invalid shortcut".into());
  }
  let key_name = *parts.last().unwrap();
  let mods = &parts[..parts.len() - 1];

  let mut enigo = Enigo::new(&Settings::default()).map_err(|e| e.to_string())?;

  for m in mods {
    let k = match *m {
      "cmd" | "command" | "meta" => Key::Meta,
      "shift" => Key::Shift,
      "alt" | "opt" | "option" => Key::Alt,
      "ctrl" | "control" => Key::Control,
      "fn" => Key::Function,
      _ => return Err(format!("unknown modifier: {m}")),
    };
    enigo.key(k, Direction::Press).map_err(|e| e.to_string())?;
  }

  tap_key(&mut enigo, key_name)?;

  for m in mods.iter().rev() {
    let k = match *m {
      "cmd" | "command" | "meta" => Key::Meta,
      "shift" => Key::Shift,
      "alt" | "opt" | "option" => Key::Alt,
      "ctrl" | "control" => Key::Control,
      "fn" => Key::Function,
      _ => continue,
    };
    enigo.key(k, Direction::Release).map_err(|e| e.to_string())?;
  }
  Ok(())
}

fn tap_key(enigo: &mut Enigo, key: &str) -> Result<(), String> {
  let k = match key {
    "return" | "enter" => Key::Return,
    "tab" => Key::Tab,
    "space" => Key::Space,
    "escape" | "esc" => Key::Escape,
    "delete" | "backspace" => Key::Backspace,
    "up" => Key::UpArrow,
    "down" => Key::DownArrow,
    "left" => Key::LeftArrow,
    "right" => Key::RightArrow,
    "volume_up" | "vol_up" | "vol+" => Key::VolumeUp,
    "volume_down" | "vol_down" | "vol-" => Key::VolumeDown,
    "mute" | "volume_mute" => Key::VolumeMute,
    "play_pause" | "playpause" => Key::MediaPlayPause,
    "next" | "next_track" => Key::MediaNextTrack,
    "previous" | "prev" | "prev_track" => Key::MediaPrevTrack,
    "f1" => Key::F1,
    "f2" => Key::F2,
    "f3" => Key::F3,
    "f4" => Key::F4,
    "f5" => Key::F5,
    "f6" => Key::F6,
    "f7" => Key::F7,
    "f8" => Key::F8,
    "f9" => Key::F9,
    "f10" => Key::F10,
    "f11" => Key::F11,
    "f12" => Key::F12,
    c if c.len() == 1 => {
      let ch = c.chars().next().unwrap();
      Key::Unicode(ch)
    }
    _ => return Err(format!("unsupported key: {key}")),
  };
  enigo.key(k, Direction::Click).map_err(|e| e.to_string())
}
