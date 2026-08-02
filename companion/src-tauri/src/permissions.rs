//! macOS permission helpers (Accessibility + Bluetooth).
//!
//! Note: on recent macOS, `AXIsProcessTrustedWithOptions(prompt)` often does
//! **not** show a system dialog for unsigned/`tauri dev` binaries. We always
//! show our own alert + open System Settings.

use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionStatus {
  pub accessibility: bool,
  pub bluetooth_ready: bool,
  pub note: String,
  /// Absolute path of this binary — must be enabled in Accessibility list.
  pub binary_path: String,
}

#[cfg(target_os = "macos")]
mod macos {
  use super::PermissionStatus;
  use core_foundation::base::TCFType;
  use core_foundation::boolean::CFBoolean;
  use core_foundation::dictionary::CFDictionary;
  use core_foundation::string::CFString;
  use std::os::raw::c_void;
  use std::process::Command;
  use std::sync::atomic::{AtomicBool, Ordering};

  static BLE_USED: AtomicBool = AtomicBool::new(false);

  #[link(name = "ApplicationServices", kind = "framework")]
  extern "C" {
    fn AXIsProcessTrusted() -> u8;
    fn AXIsProcessTrustedWithOptions(options: *const c_void) -> u8;
  }

  pub fn current_exe() -> String {
    std::env::current_exe()
      .map(|p| p.display().to_string())
      .unwrap_or_else(|_| "(unknown)".into())
  }

  pub fn mark_bluetooth_used() {
    BLE_USED.store(true, Ordering::SeqCst);
  }

  /// Restore Bluetooth “ready” flag from disk after a prior successful Scan/Connect.
  pub fn hydrate_bluetooth_ready(ready: bool) {
    if ready {
      BLE_USED.store(true, Ordering::SeqCst);
    }
  }

  pub fn bluetooth_ready() -> bool {
    BLE_USED.load(Ordering::SeqCst)
  }

  pub fn accessibility_trusted() -> bool {
    unsafe { AXIsProcessTrusted() != 0 }
  }

  fn osascript(script: &str) -> Result<String, String> {
    let out = Command::new("/usr/bin/osascript")
      .args(["-e", script])
      .output()
      .map_err(|e| e.to_string())?;
    if !out.status.success() {
      return Err(String::from_utf8_lossy(&out.stderr).trim().to_string());
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
  }

  /// Show a blocking macOS alert (always visible, including in `tauri dev`).
  pub fn show_alert(title: &str, message: &str, button: &str) {
    let title = title.replace('"', "\\\"");
    let message = message.replace('"', "\\\"");
    let button = button.replace('"', "\\\"");
    let script = format!(
      r#"display alert "{title}" message "{message}" buttons {{"{button}"}} default button 1"#
    );
    let _ = osascript(&script);
  }

  /// Try system AX prompt (may be a no-op on Sequoia for unsigned apps).
  pub fn request_accessibility() -> bool {
    let trusted = unsafe {
      let key = CFString::new("AXTrustedCheckOptionPrompt");
      let value = CFBoolean::true_value();
      let dict = CFDictionary::from_CFType_pairs(&[(key, value)]);
      AXIsProcessTrustedWithOptions(dict.as_concrete_TypeRef() as *const c_void) != 0
    };
    let path = current_exe();
    show_alert(
      "TouchDeck — Accessibility",
      &format!(
        "Enable Accessibility for TouchDeck Companion.\n\n\
         1. In the window that opens: Privacy & Security → Accessibility\n\
         2. Turn on the toggle for «TouchDeck Companion» or this binary:\n{path}\n\
         3. Return to the app and press Scan BLE."
      ),
      "Open Settings",
    );
    let _ = open_accessibility_settings();
    trusted || accessibility_trusted()
  }

  pub fn request_bluetooth() -> Result<(), String> {
    show_alert(
      "TouchDeck — Bluetooth",
      "macOS needs Bluetooth permission to Scan / Connect to the deck.\n\n\
       1. Allow when the system dialog appears (if shown)\n\
       2. Or System Settings → Privacy & Security → Bluetooth\n\
       3. Enable TouchDeck Companion, then Scan again.",
      "Continue",
    );
    let _ = open_bluetooth_settings();
    Ok(())
  }

  pub fn open_accessibility_settings() -> Result<(), String> {
    // Prefer `open` with the modern Settings URL; fall back to older pane.
    let attempts = [
      [
        "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Accessibility",
      ]
      .as_slice(),
      ["x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"].as_slice(),
    ];
    for urls in attempts {
      for url in urls {
        if Command::new("/usr/bin/open")
          .arg(url)
          .status()
          .map(|s| s.success())
          .unwrap_or(false)
        {
          return Ok(());
        }
      }
    }
    // Last resort: open System Settings app
    let _ = Command::new("/usr/bin/open").arg("-a").arg("System Settings").status();
    Ok(())
  }

  pub fn open_bluetooth_settings() -> Result<(), String> {
    let urls = [
      "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Bluetooth",
      "x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth",
    ];
    for url in urls {
      if Command::new("/usr/bin/open")
        .arg(url)
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
      {
        return Ok(());
      }
    }
    let _ = Command::new("/usr/bin/open").arg("-a").arg("System Settings").status();
    Ok(())
  }

  pub fn status() -> PermissionStatus {
    let accessibility = accessibility_trusted();
    PermissionStatus {
      accessibility,
      bluetooth_ready: BLE_USED.load(Ordering::SeqCst),
      note: if accessibility {
        "Accessibility OK.".into()
      } else {
        "Enable Accessibility in System Settings.".into()
      },
      binary_path: current_exe(),
    }
  }
}

#[cfg(target_os = "macos")]
pub use macos::*;

#[cfg(not(target_os = "macos"))]
pub fn mark_bluetooth_used() {}

#[cfg(not(target_os = "macos"))]
pub fn hydrate_bluetooth_ready(_ready: bool) {}

#[cfg(not(target_os = "macos"))]
pub fn bluetooth_ready() -> bool {
  true
}

#[cfg(not(target_os = "macos"))]
pub fn accessibility_trusted() -> bool {
  true
}

#[cfg(not(target_os = "macos"))]
pub fn request_accessibility() -> bool {
  true
}

#[cfg(not(target_os = "macos"))]
pub fn request_bluetooth() -> Result<(), String> {
  Ok(())
}

#[cfg(not(target_os = "macos"))]
pub fn open_accessibility_settings() -> Result<(), String> {
  Err("macOS only".into())
}

#[cfg(not(target_os = "macos"))]
pub fn open_bluetooth_settings() -> Result<(), String> {
  Err("macOS only".into())
}

#[cfg(not(target_os = "macos"))]
pub fn status() -> PermissionStatus {
  PermissionStatus {
    accessibility: true,
    bluetooth_ready: true,
    note: "Permissions are managed by the OS.".into(),
    binary_path: String::new(),
  }
}
