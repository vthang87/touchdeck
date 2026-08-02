//! macOS Now Playing poller + seek helpers.
//!
//! On macOS 15.4+, direct MediaRemote.framework access is entitlement-gated.
//! Prefer JXA `MRNowPlayingRequest` (works system-wide), then Music/Spotify
//! AppleScript, then the private MediaRemote FFI as a last resort.

use serde::Serialize;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct NowPlaying {
  pub title: String,
  pub artist: String,
  pub playing: bool,
  pub pos_ms: u32,
  pub dur_ms: u32,
  pub app: String,
  /// Playback rate × 100 (100 = 1.0×).
  pub rate_x100: u16,
}

impl Default for NowPlaying {
  fn default() -> Self {
    Self {
      title: String::new(),
      artist: String::new(),
      playing: false,
      pos_ms: 0,
      dur_ms: 0,
      app: String::new(),
      rate_x100: 100,
    }
  }
}

impl NowPlaying {
  /// Keep GATT payloads small (UTF-8 + JSON overhead).
  pub fn truncated(mut self) -> Self {
    self.title = truncate_chars(&self.title, 72);
    self.artist = truncate_chars(&self.artist, 48);
    self.app = truncate_chars(&self.app, 24);
    self
  }
}

fn truncate_chars(s: &str, max: usize) -> String {
  if s.chars().count() <= max {
    return s.to_string();
  }
  let mut out: String = s.chars().take(max.saturating_sub(1)).collect();
  out.push('…');
  out
}

fn run_cmd_timeout(program: &str, args: &[&str], timeout: std::time::Duration) -> Result<String, String> {
  use std::io::Read;
  use std::process::{Command, Stdio};
  use std::time::Instant;

  let mut child = Command::new(program)
    .args(args)
    .stdout(Stdio::piped())
    .stderr(Stdio::piped())
    .spawn()
    .map_err(|e| e.to_string())?;
  let start = Instant::now();
  loop {
    match child.try_wait() {
      Ok(Some(status)) => {
        let mut stdout = String::new();
        if let Some(mut out) = child.stdout.take() {
          let _ = out.read_to_string(&mut stdout);
        }
        let mut stderr = String::new();
        if let Some(mut err) = child.stderr.take() {
          let _ = err.read_to_string(&mut stderr);
        }
        if !status.success() {
          return Err(format!("{program} failed: {}", stderr.trim()));
        }
        return Ok(stdout.trim().to_string());
      }
      Ok(None) if start.elapsed() >= timeout => {
        let _ = child.kill();
        let _ = child.wait();
        return Err(format!("{program} timeout"));
      }
      Ok(None) => std::thread::sleep(std::time::Duration::from_millis(40)),
      Err(e) => return Err(e.to_string()),
    }
  }
}

fn run_osascript(script: &str) -> Result<String, String> {
  run_cmd_timeout(
    "/usr/bin/osascript",
    &["-e", script],
    std::time::Duration::from_secs(2),
  )
}

fn run_jxa(script: &str) -> Result<String, String> {
  run_cmd_timeout(
    "/usr/bin/osascript",
    &["-l", "JavaScript", "-e", script],
    std::time::Duration::from_secs(2),
  )
}

fn parse_ms(sec: &str) -> u32 {
  let s: f64 = sec.trim().parse().unwrap_or(0.0);
  if s < 0.0 {
    0
  } else {
    (s * 1000.0) as u32
  }
}

fn secs_to_ms(v: f64) -> u32 {
  if v <= 0.0 {
    0
  } else {
    (v * 1000.0) as u32
  }
}

/// System-wide Now Playing via private MRNowPlayingRequest (macOS 15.4+ safe).
fn poll_jxa() -> Option<NowPlaying> {
  const SCRIPT: &str = r#"
ObjC.import('Foundation');
function run() {
  try {
    const MediaRemote = $.NSBundle.bundleWithPath(
      '/System/Library/PrivateFrameworks/MediaRemote.framework/'
    );
    MediaRemote.load;
    const MRNowPlayingRequest = $.NSClassFromString('MRNowPlayingRequest');
    if (!MRNowPlayingRequest) return '';
    let app = '';
    try {
      const path = MRNowPlayingRequest.localNowPlayingPlayerPath;
      if (path && path.client) app = path.client.displayName.js || '';
    } catch (e) {}
    const item = MRNowPlayingRequest.localNowPlayingItem;
    if (!item) return JSON.stringify({ title: '', artist: '', playing: false, pos_ms: 0, dur_ms: 0, app: app });
    const info = item.nowPlayingInfo;
    if (!info) return JSON.stringify({ title: '', artist: '', playing: false, pos_ms: 0, dur_ms: 0, app: app });
    const g = (k) => {
      try {
        const v = info.valueForKey(k);
        return v ? v.js : null;
      } catch (e) { return null; }
    };
    const title = g('kMRMediaRemoteNowPlayingInfoTitle') || '';
    const artist = g('kMRMediaRemoteNowPlayingInfoArtist') || '';
    const dur = Number(g('kMRMediaRemoteNowPlayingInfoDuration') || 0);
    const elapsed = Number(g('kMRMediaRemoteNowPlayingInfoElapsedTime') || 0);
    const rate = Number(g('kMRMediaRemoteNowPlayingInfoPlaybackRate') || 0);
    const playing = rate > 0.01;
    const rateAbs = rate > 0.01 ? rate : 1;
    return JSON.stringify({
      title: String(title),
      artist: String(artist),
      playing: playing,
      pos_ms: Math.max(0, Math.round(elapsed * 1000)),
      dur_ms: Math.max(0, Math.round(dur * 1000)),
      app: String(app),
      rate_x100: Math.max(25, Math.min(400, Math.round(rateAbs * 100)))
    });
  } catch (e) {
    return '';
  }
}
"#;
  let raw = run_jxa(SCRIPT).ok()?;
  if raw.is_empty() {
    return None;
  }
  let v: serde_json::Value = serde_json::from_str(&raw).ok()?;
  let title = v
    .get("title")
    .and_then(|x| x.as_str())
    .unwrap_or("")
    .to_string();
  let artist = v
    .get("artist")
    .and_then(|x| x.as_str())
    .unwrap_or("")
    .to_string();
  let playing = v.get("playing").and_then(|x| x.as_bool()).unwrap_or(false);
  let pos_ms = v.get("pos_ms").and_then(|x| x.as_u64()).unwrap_or(0) as u32;
  let dur_ms = v.get("dur_ms").and_then(|x| x.as_u64()).unwrap_or(0) as u32;
  let app = v
    .get("app")
    .and_then(|x| x.as_str())
    .unwrap_or("")
    .to_string();
  let rate_x100 = v
    .get("rate_x100")
    .and_then(|x| x.as_u64())
    .unwrap_or(100)
    .clamp(25, 400) as u16;
  if title.is_empty() && !playing {
    // Still useful to clear the board / show app idle — return Some only if we have signal.
    if app.is_empty() {
      return Some(NowPlaying::default());
    }
  }
  Some(NowPlaying {
    title,
    artist,
    playing,
    pos_ms,
    dur_ms,
    app,
    rate_x100,
  })
}

#[cfg(target_os = "macos")]
mod media_remote {
  use super::NowPlaying;
  use std::ffi::CStr;
  use std::os::raw::c_char;

  extern "C" {
    fn touchdeck_now_playing_poll_async();
    fn touchdeck_now_playing_json_copy() -> *mut c_char;
    fn touchdeck_now_playing_free(p: *mut c_char);
    fn touchdeck_now_playing_set_elapsed(elapsed_sec: f64) -> i32;
    fn touchdeck_now_playing_set_speed_x100(speed_x100: i32) -> i32;
  }

  pub fn kick() {
    unsafe { touchdeck_now_playing_poll_async() };
  }

  pub fn set_elapsed_seconds(elapsed_sec: f64) -> bool {
    unsafe { touchdeck_now_playing_set_elapsed(elapsed_sec) != 0 }
  }

  pub fn set_speed_x100(speed_x100: u16) -> bool {
    unsafe { touchdeck_now_playing_set_speed_x100(speed_x100 as i32) != 0 }
  }

  pub fn snapshot() -> NowPlaying {
    unsafe {
      let ptr = touchdeck_now_playing_json_copy();
      if ptr.is_null() {
        return NowPlaying::default();
      }
      let json = CStr::from_ptr(ptr).to_string_lossy().into_owned();
      touchdeck_now_playing_free(ptr);
      if let Ok(v) = serde_json::from_str::<serde_json::Value>(&json) {
        return NowPlaying {
          title: v
            .get("title")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
          artist: v
            .get("artist")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
          playing: v.get("playing").and_then(|x| x.as_bool()).unwrap_or(false),
          pos_ms: v.get("pos_ms").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
          dur_ms: v.get("dur_ms").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
          app: v
            .get("app")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
          rate_x100: v
            .get("rate_x100")
            .and_then(|x| x.as_u64())
            .unwrap_or(100)
            .clamp(25, 400) as u16,
        };
      }
    }
    NowPlaying::default()
  }
}

fn poll_app(app: &str) -> Option<NowPlaying> {
  let script = format!(
    r#"
try
  if application "{app}" is not running then return ""
  tell application "{app}"
    set st to player state as text
    if st is "stopped" then return ""
    set t to name of current track
    set a to artist of current track
    set p to player position
    try
      set d to duration of current track
    on error
      set d to 0
    end try
    return st & "|" & t & "|" & a & "|" & p & "|" & d
  end tell
on error
  return ""
end try
"#
  );
  let raw = run_osascript(&script).ok()?;
  if raw.is_empty() {
    return None;
  }
  let parts: Vec<&str> = raw.splitn(5, '|').collect();
  if parts.len() < 5 {
    return None;
  }
  let playing = parts[0].eq_ignore_ascii_case("playing");
  // Music: duration in seconds. Spotify AppleScript: often milliseconds.
  let dur_raw: f64 = parts[4].trim().parse().unwrap_or(0.0);
  let dur_ms = if app == "Spotify" && dur_raw > 1000.0 {
    dur_raw as u32
  } else {
    secs_to_ms(dur_raw)
  };
  Some(NowPlaying {
    title: parts[1].to_string(),
    artist: parts[2].to_string(),
    playing,
    pos_ms: parse_ms(parts[3]),
    dur_ms,
    app: app.to_string(),
    rate_x100: 100,
  })
}

/// Common playback-rate ladder (×100).
const RATE_STEPS: &[u16] = &[75, 100, 125, 150, 175, 200];

fn nearest_rate_step(rate_x100: u16) -> usize {
  let mut best = 0usize;
  let mut best_d = u16::MAX;
  for (i, &s) in RATE_STEPS.iter().enumerate() {
    let d = if s >= rate_x100 {
      s - rate_x100
    } else {
      rate_x100 - s
    };
    if d < best_d {
      best_d = d;
      best = i;
    }
  }
  best
}

fn set_browser_video_rate(app: &str, rate: f64) -> Result<(), String> {
  let rate_s = format!("{rate:.2}");
  let js = format!(
    "(function(){{var vs=document.querySelectorAll('video');if(!vs.length)return 'no';for(var i=0;i<vs.length;i++)vs[i].playbackRate={rate_s};return String(vs[0].playbackRate);}})()"
  );
  let app_l = app.to_lowercase();
  if app_l.contains("safari") {
    let script = format!(
      r#"
try
  tell application "Safari"
    if (count of windows) is 0 then return "no"
    set r to do JavaScript "{js}" in front document
    return r as text
  end tell
on error e
  return "err:" & e
end try
"#
    );
    let out = run_osascript(&script)?;
    if out.starts_with("err:") || out == "no" || out.is_empty() {
      return Err(out);
    }
    return Ok(());
  }
  if app_l.contains("chrome") {
    let script = format!(
      r#"
try
  tell application "Google Chrome"
    if (count of windows) is 0 then return "no"
    set r to execute active tab of front window javascript "{js}"
    return r as text
  end tell
on error e
  return "err:" & e
end try
"#
    );
    let out = run_osascript(&script)?;
    if out.starts_with("err:") || out == "no" || out.is_empty() {
      return Err(out);
    }
    return Ok(());
  }
  Err("not a browser".into())
}

fn youtube_rate_hotkey(faster: bool) -> Result<(), String> {
  // YouTube: Shift+. faster / Shift+, slower (when player focused / Safari frontmost).
  let key = if faster { ">" } else { "<" };
  let script = format!(
    r#"
try
  tell application "Safari" to activate
  delay 0.12
  tell application "System Events" to keystroke "{key}" using shift down
  return "ok"
on error e
  return "err:" & e
end try
"#
  );
  let out = run_osascript(&script)?;
  if out == "ok" {
    Ok(())
  } else {
    Err(out)
  }
}

/// Set absolute playback rate (×100). Best-effort across MediaRemote / browser / YouTube.
pub fn set_rate_x100(rate_x100: u16) -> Result<u16, String> {
  let rate_x100 = rate_x100.clamp(25, 400);
  let rate = f64::from(rate_x100) / 100.0;
  let np = poll_snapshot();

  #[cfg(target_os = "macos")]
  {
    let _ = media_remote::set_speed_x100(rate_x100);
    if !np.app.is_empty() {
      if set_browser_video_rate(&np.app, rate).is_ok() {
        return Ok(rate_x100);
      }
    }
    // Relative hotkey toward target (YouTube in Safari).
    let cur = if np.rate_x100 == 0 { 100 } else { np.rate_x100 };
    let cur_i = nearest_rate_step(cur);
    let tgt_i = nearest_rate_step(rate_x100);
    if tgt_i != cur_i && np.app.to_lowercase().contains("safari") {
      let faster = tgt_i > cur_i;
      let steps = if faster { tgt_i - cur_i } else { cur_i - tgt_i };
      for _ in 0..steps {
        let _ = youtube_rate_hotkey(faster);
        std::thread::sleep(std::time::Duration::from_millis(80));
      }
      return Ok(RATE_STEPS[tgt_i]);
    }
  }
  let _ = np;
  let _ = rate;
  Err("playback rate not supported for this player".into())
}

/// Nudge playback rate one step (−1 / +1) on the common ladder.
pub fn nudge_rate(dir: i32) -> Result<u16, String> {
  let np = poll_snapshot();
  let cur = if np.rate_x100 == 0 { 100 } else { np.rate_x100 };
  let mut idx = nearest_rate_step(cur) as i32;
  idx = (idx + dir).clamp(0, (RATE_STEPS.len() as i32) - 1);
  let target = RATE_STEPS[idx as usize];
  // Prefer browser absolute set; hotkey nudge as fallback for YouTube.
  match set_rate_x100(target) {
    Ok(r) => Ok(r),
    Err(_) if np.app.to_lowercase().contains("safari") => {
      youtube_rate_hotkey(dir > 0)?;
      Ok(target)
    }
    Err(e) => Err(e),
  }
}

#[cfg(target_os = "macos")]
pub fn media_remote_kick() {
  media_remote::kick();
}

#[cfg(not(target_os = "macos"))]
pub fn media_remote_kick() {}

/// Read Now Playing: JXA first, then app scripts, then MediaRemote FFI.
pub fn poll_snapshot() -> NowPlaying {
  #[cfg(target_os = "macos")]
  {
    if let Some(np) = poll_jxa() {
      if !np.title.is_empty() || np.playing || !np.app.is_empty() {
        return np;
      }
    }
    if let Some(np) = poll_app("Music") {
      if !np.title.is_empty() || np.playing {
        return np;
      }
    }
    if let Some(np) = poll_app("Spotify") {
      if !np.title.is_empty() || np.playing {
        return np;
      }
    }
    let mr = media_remote::snapshot();
    if !mr.title.is_empty() || mr.playing {
      return mr;
    }
  }
  NowPlaying::default()
}

/// Poll system Now Playing (JXA first — MediaRemote FFI is gated on macOS 15.4+).
pub fn poll() -> NowPlaying {
  poll_snapshot().truncated()
}

/// Seek ±seconds in the active system Now Playing session.
pub fn seek(delta_sec: i32) -> Result<(), String> {
  #[cfg(not(target_os = "macos"))]
  {
    let _ = delta_sec;
    return Err("seek not supported on this OS".into());
  }
  #[cfg(target_os = "macos")]
  {
    // Position from JXA (works on 15.4+); write via MediaRemote SetElapsedTime.
    let np = poll_snapshot();
    if !np.title.is_empty() || np.playing || np.pos_ms > 0 || np.dur_ms > 0 {
      let mut next = (np.pos_ms as f64) / 1000.0 + f64::from(delta_sec);
      if next < 0.0 {
        next = 0.0;
      }
      if np.dur_ms > 1000 {
        let dur = (np.dur_ms as f64) / 1000.0;
        if next > dur {
          next = dur;
        }
      }
      if media_remote::set_elapsed_seconds(next) {
        return Ok(());
      }
    }
    for app in ["Music", "Spotify"] {
      let script = format!(
        r#"
try
  if application "{app}" is not running then return "skip"
  tell application "{app}"
    if player state is stopped then return "skip"
    set player position to (player position + ({delta_sec}))
    return "ok"
  end tell
on error
  return "err"
end try
"#
      );
      match run_osascript(&script) {
        Ok(s) if s == "ok" => return Ok(()),
        Ok(_) => continue,
        Err(_) => continue,
      }
    }
    Err("seek failed — no active Now Playing session".into())
  }
}

#[cfg(test)]
mod tests {
  #[test]
  fn jxa_smoke() {
    let np = super::poll();
    eprintln!("NP = {np:?}");
    // Soft check: poll must return without hanging; title may be empty if idle.
    let _ = np.title;
  }
}

#[cfg(test)]
mod seek_tests {
  #[test]
  fn seek_smoke() {
    let before = super::poll();
    eprintln!("before pos={}", before.pos_ms);
    if before.pos_ms == 0 && before.title.is_empty() {
      eprintln!("skip: nothing playing");
      return;
    }
    let r = super::seek(10);
    eprintln!("seek result={r:?}");
    std::thread::sleep(std::time::Duration::from_millis(700));
    let after = super::poll();
    eprintln!("after pos={}", after.pos_ms);
    assert!(r.is_ok(), "seek should ok: {r:?}");
    let delta = after.pos_ms as i64 - before.pos_ms as i64;
    assert!(
      delta > 5000 && delta < 20000,
      "expected ~+10s seek, delta_ms={delta}"
    );
  }
}
