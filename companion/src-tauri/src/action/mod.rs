pub mod engine;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ActionKind {
  OpenApp,
  OpenUrl,
  Media,
  Volume,
  Mute,
  /// Simulate keystrokes / shortcuts (e.g. `cmd+c`, `cmd+shift+p`, or type `hello`).
  Keyboard,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActionRecord {
  pub action_id: String,
  pub kind: ActionKind,
  /// Bundle ID, path, URL, media op, or keyboard spec (`cmd+c` / text to type).
  pub value: String,
  pub label: String,
}

#[derive(Debug, Clone)]
pub struct VolumeState {
  pub level: u8,
  pub muted: bool,
}
