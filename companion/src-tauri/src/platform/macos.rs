use std::process::Command;

use crate::action::VolumeState;
use crate::platform::{sanitize_bundle, sanitize_path};

pub fn open_app(target: &str) -> Result<(), String> {
  if target.starts_with('/') {
    let path = sanitize_path(target)?;
    let status = Command::new("/usr/bin/open")
      .arg(path)
      .status()
      .map_err(|e| e.to_string())?;
    if !status.success() {
      return Err(format!("open failed: {status}"));
    }
    return Ok(());
  }
  let bundle = sanitize_bundle(target)?;
  let status = Command::new("/usr/bin/open")
    .args(["-b", bundle])
    .status()
    .map_err(|e| e.to_string())?;
  if !status.success() {
    return Err(format!("open -b failed: {status}"));
  }
  Ok(())
}

pub fn open_url(url: &str) -> Result<(), String> {
  if !(url.starts_with("http://") || url.starts_with("https://")) {
    return Err("url must be http(s)".into());
  }
  let status = Command::new("/usr/bin/open")
    .arg(url)
    .status()
    .map_err(|e| e.to_string())?;
  if !status.success() {
    return Err(format!("open url failed: {status}"));
  }
  Ok(())
}

fn run_osascript(script: &str) -> Result<String, String> {
  let out = Command::new("/usr/bin/osascript")
    .args(["-e", script])
    .output()
    .map_err(|e| e.to_string())?;
  if !out.status.success() {
    return Err(format!(
      "osascript failed: {}",
      String::from_utf8_lossy(&out.stderr)
    ));
  }
  Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}

pub fn read_volume() -> Result<VolumeState, String> {
  let raw = run_osascript(
    "set o to get volume settings\n\
     return (output volume of o as text) & \",\" & (output muted of o as text)",
  )?;
  let mut parts = raw.split(',');
  let level: u8 = parts
    .next()
    .ok_or("bad volume")?
    .trim()
    .parse()
    .map_err(|_| "bad volume level".to_string())?;
  let muted = parts.next().unwrap_or("false").trim() == "true";
  Ok(VolumeState { level, muted })
}

pub fn adjust_volume(delta: i32) -> Result<VolumeState, String> {
  let cur = read_volume()?;
  let next = (cur.level as i32 + delta).clamp(0, 100) as u8;
  run_osascript(&format!("set volume output volume {next}"))?;
  if cur.muted {
    run_osascript("set volume without output muted")?;
  }
  read_volume()
}

pub fn toggle_mute() -> Result<VolumeState, String> {
  let cur = read_volume()?;
  if cur.muted {
    run_osascript("set volume without output muted")?;
  } else {
    run_osascript("set volume with output muted")?;
  }
  read_volume()
}

/// Media keys via System Events (requires Accessibility permission).
pub fn media_key(op: &str) -> Result<(), String> {
  match op {
    "seek_fwd" | "media_seek_fwd" => return crate::now_playing::seek(10),
    "seek_back" | "media_seek_back" => return crate::now_playing::seek(-10),
    "rate_up" | "media_rate_up" => {
      crate::now_playing::nudge_rate(1)?;
      return Ok(());
    }
    "rate_down" | "media_rate_down" => {
      crate::now_playing::nudge_rate(-1)?;
      return Ok(());
    }
    "rate_1x" | "media_rate_1x" => {
      crate::now_playing::set_rate_x100(100)?;
      return Ok(());
    }
    _ => {}
  }
  let script = match op {
    "play_pause" | "playpause" | "media_play_pause" => {
      "tell application \"System Events\" to key code 16"
    }
    "next" | "media_next" => "tell application \"System Events\" to key code 17",
    "previous" | "prev" | "media_previous" => {
      "tell application \"System Events\" to key code 18"
    }
    _ => return Err(format!("unknown media op: {op}")),
  };
  run_osascript(script)?;
  Ok(())
}

/// Simulate keyboard input via System Events (Accessibility required).
///
/// Formats:
/// - Shortcut: `cmd+c`, `cmd+shift+p`, `ctrl+tab`, `opt+space`, `cmd+return`
/// - Type text: any other string (escaped) → `keystroke "…"`
pub fn keyboard(spec: &str) -> Result<(), String> {
  let spec = spec.trim();
  if spec.is_empty() {
    return Err("empty keyboard spec".into());
  }
  if spec.contains('+') {
    return keyboard_shortcut(spec);
  }
  // Plain text typing
  let escaped = spec.replace('\\', "\\\\").replace('"', "\\\"");
  if escaped.chars().any(|c| ";|&`$<>\n\r".contains(c)) {
    return Err("keyboard text has forbidden characters".into());
  }
  if escaped.len() > 120 {
    return Err("keyboard text too long".into());
  }
  run_osascript(&format!(
    r#"tell application "System Events" to keystroke "{escaped}""#
  ))?;
  Ok(())
}

fn keyboard_shortcut(spec: &str) -> Result<(), String> {
  let lower = spec.to_lowercase();
  let parts: Vec<&str> = lower.split('+').map(|s| s.trim()).filter(|s| !s.is_empty()).collect();
  if parts.is_empty() {
    return Err("invalid shortcut".into());
  }
  let key = parts.last().copied().unwrap();
  let mods = &parts[..parts.len() - 1];

  let mut using: Vec<&str> = Vec::new();
  for m in mods {
    match *m {
      "cmd" | "command" | "meta" => using.push("command down"),
      "shift" => using.push("shift down"),
      "alt" | "opt" | "option" => using.push("option down"),
      "ctrl" | "control" => using.push("control down"),
      "fn" => using.push("fn down"),
      _ => return Err(format!("unknown modifier: {m}")),
    }
  }

  let using_clause = if using.is_empty() {
    String::new()
  } else {
    format!(" using {{{}}}", using.join(", "))
  };

  // Special named keys → key code
  let script = match key {
    "return" | "enter" => format!(
      r#"tell application "System Events" to key code 36{using_clause}"#
    ),
    "tab" => format!(r#"tell application "System Events" to key code 48{using_clause}"#),
    "space" => format!(r#"tell application "System Events" to key code 49{using_clause}"#),
    "escape" | "esc" => {
      format!(r#"tell application "System Events" to key code 53{using_clause}"#)
    },
    "delete" | "backspace" => {
      format!(r#"tell application "System Events" to key code 51{using_clause}"#)
    },
    "up" => format!(r#"tell application "System Events" to key code 126{using_clause}"#),
    "down" => format!(r#"tell application "System Events" to key code 125{using_clause}"#),
    "left" => format!(r#"tell application "System Events" to key code 123{using_clause}"#),
    "right" => format!(r#"tell application "System Events" to key code 124{using_clause}"#),
    // Mac volume / media key codes (Option+Shift+Vol = fine steps).
    "volume_up" | "vol_up" | "vol+" => {
      format!(r#"tell application "System Events" to key code 72{using_clause}"#)
    }
    "volume_down" | "vol_down" | "vol-" => {
      format!(r#"tell application "System Events" to key code 73{using_clause}"#)
    }
    "mute" | "volume_mute" => {
      format!(r#"tell application "System Events" to key code 74{using_clause}"#)
    }
    k if k.len() == 1 && k.chars().all(|c| c.is_ascii_alphanumeric()) => {
      format!(r#"tell application "System Events" to keystroke "{k}"{using_clause}"#)
    }
    _ => return Err(format!("unsupported key: {key}")),
  };
  run_osascript(&script)?;
  Ok(())
}
