use std::path::PathBuf;

use rusqlite::{params, Connection};
use thiserror::Error;

pub use crate::action::{ActionKind, ActionRecord};

#[derive(Debug, Error)]
pub enum StoreError {
  #[error("{0}")]
  Msg(String),
}

pub struct Store {
  conn: Connection,
}

impl Store {
  pub fn open_default() -> Result<Self, StoreError> {
    let dir = dirs::data_dir()
      .unwrap_or_else(|| PathBuf::from("."))
      .join("TouchDeck Companion");
    std::fs::create_dir_all(&dir).map_err(|e| StoreError::Msg(e.to_string()))?;
    let path = dir.join("touchdeck.sqlite");
    Self::open(&path)
  }

  pub fn open(path: &std::path::Path) -> Result<Self, StoreError> {
    let conn = Connection::open(path).map_err(|e| StoreError::Msg(e.to_string()))?;
    let store = Self { conn };
    store.migrate()?;
    Ok(store)
  }

  fn migrate(&self) -> Result<(), StoreError> {
    self
      .conn
      .execute_batch(
        "CREATE TABLE IF NOT EXISTS actions (
            action_id TEXT PRIMARY KEY NOT NULL,
            kind TEXT NOT NULL,
            value TEXT NOT NULL,
            label TEXT NOT NULL DEFAULT ''
         );
         CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL
         );",
      )
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    Ok(())
  }

  pub fn seed_defaults(&self) -> Result<(), StoreError> {
    for a in default_actions() {
      // Insert missing only — never overwrite user edits.
      if self.get_action(&a.action_id)?.is_none() {
        self.upsert_action(&a)?;
      }
    }
    Ok(())
  }

  pub fn list_actions(&self) -> Result<Vec<ActionRecord>, StoreError> {
    let mut stmt = self
      .conn
      .prepare("SELECT action_id, kind, value, label FROM actions ORDER BY label, action_id")
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    let rows = stmt
      .query_map([], |row| {
        let kind_s: String = row.get(1)?;
        Ok(ActionRecord {
          action_id: row.get(0)?,
          kind: kind_from_str(&kind_s),
          value: row.get(2)?,
          label: row.get(3)?,
        })
      })
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    let mut out = Vec::new();
    for r in rows {
      out.push(r.map_err(|e| StoreError::Msg(e.to_string()))?);
    }
    Ok(out)
  }

  pub fn get_action(&self, action_id: &str) -> Result<Option<ActionRecord>, StoreError> {
    let mut stmt = self
      .conn
      .prepare("SELECT action_id, kind, value, label FROM actions WHERE action_id = ?1")
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    let mut rows = stmt
      .query(params![action_id])
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    if let Some(row) = rows.next().map_err(|e| StoreError::Msg(e.to_string()))? {
      let kind_s: String = row.get(1).map_err(|e| StoreError::Msg(e.to_string()))?;
      Ok(Some(ActionRecord {
        action_id: row.get(0).map_err(|e| StoreError::Msg(e.to_string()))?,
        kind: kind_from_str(&kind_s),
        value: row.get(2).map_err(|e| StoreError::Msg(e.to_string()))?,
        label: row.get(3).map_err(|e| StoreError::Msg(e.to_string()))?,
      }))
    } else {
      Ok(None)
    }
  }

  pub fn upsert_action(&self, action: &ActionRecord) -> Result<(), StoreError> {
    self
      .conn
      .execute(
        "INSERT INTO actions (action_id, kind, value, label) VALUES (?1, ?2, ?3, ?4)
         ON CONFLICT(action_id) DO UPDATE SET kind=excluded.kind, value=excluded.value, label=excluded.label",
        params![
          action.action_id,
          kind_to_str(&action.kind),
          action.value,
          action.label
        ],
      )
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    Ok(())
  }

  pub fn delete_action(&self, action_id: &str) -> Result<(), StoreError> {
    self
      .conn
      .execute("DELETE FROM actions WHERE action_id = ?1", params![action_id])
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    Ok(())
  }

  pub fn get_setting(&self, key: &str) -> Result<Option<String>, StoreError> {
    let mut stmt = self
      .conn
      .prepare("SELECT value FROM settings WHERE key = ?1")
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    let mut rows = stmt
      .query(params![key])
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    if let Some(row) = rows.next().map_err(|e| StoreError::Msg(e.to_string()))? {
      Ok(Some(row.get(0).map_err(|e| StoreError::Msg(e.to_string()))?))
    } else {
      Ok(None)
    }
  }

  pub fn set_setting(&self, key: &str, value: &str) -> Result<(), StoreError> {
    self
      .conn
      .execute(
        "INSERT INTO settings (key, value) VALUES (?1, ?2)
         ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        params![key, value],
      )
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    Ok(())
  }

  pub fn delete_setting(&self, key: &str) -> Result<(), StoreError> {
    self
      .conn
      .execute("DELETE FROM settings WHERE key = ?1", params![key])
      .map_err(|e| StoreError::Msg(e.to_string()))?;
    Ok(())
  }
}

fn kind_to_str(k: &ActionKind) -> &'static str {
  match k {
    ActionKind::OpenApp => "open_app",
    ActionKind::OpenUrl => "open_url",
    ActionKind::Media => "media",
    ActionKind::Volume => "volume",
    ActionKind::Mute => "mute",
    ActionKind::Keyboard => "keyboard",
  }
}

fn kind_from_str(s: &str) -> ActionKind {
  match s {
    "open_url" => ActionKind::OpenUrl,
    "media" => ActionKind::Media,
    "volume" => ActionKind::Volume,
    "mute" => ActionKind::Mute,
    "keyboard" => ActionKind::Keyboard,
    _ => ActionKind::OpenApp,
  }
}

fn default_actions() -> Vec<ActionRecord> {
  vec![
    ActionRecord {
      action_id: "volume_up".into(),
      kind: ActionKind::Volume,
      value: "volume_up".into(),
      label: "Volume Up".into(),
    },
    ActionRecord {
      action_id: "volume_down".into(),
      kind: ActionKind::Volume,
      value: "volume_down".into(),
      label: "Volume Down".into(),
    },
    ActionRecord {
      action_id: "mute".into(),
      kind: ActionKind::Mute,
      value: "mute".into(),
      label: "Mute".into(),
    },
    ActionRecord {
      action_id: "play_pause".into(),
      kind: ActionKind::Media,
      value: "play_pause".into(),
      label: "Play/Pause".into(),
    },
    ActionRecord {
      action_id: "next".into(),
      kind: ActionKind::Media,
      value: "next".into(),
      label: "Next".into(),
    },
    ActionRecord {
      action_id: "previous".into(),
      kind: ActionKind::Media,
      value: "previous".into(),
      label: "Previous".into(),
    },
    ActionRecord {
      action_id: "media_play_pause".into(),
      kind: ActionKind::Media,
      value: "play_pause".into(),
      label: "Media Play/Pause".into(),
    },
    ActionRecord {
      action_id: "media_next".into(),
      kind: ActionKind::Media,
      value: "next".into(),
      label: "Media Next".into(),
    },
    ActionRecord {
      action_id: "media_previous".into(),
      kind: ActionKind::Media,
      value: "previous".into(),
      label: "Media Previous".into(),
    },
    ActionRecord {
      action_id: "media_seek_fwd".into(),
      kind: ActionKind::Media,
      value: "seek_fwd".into(),
      label: "Seek +10s".into(),
    },
    ActionRecord {
      action_id: "media_seek_back".into(),
      kind: ActionKind::Media,
      value: "seek_back".into(),
      label: "Seek −10s".into(),
    },
    ActionRecord {
      action_id: "media_rate_up".into(),
      kind: ActionKind::Media,
      value: "rate_up".into(),
      label: "Speed +".into(),
    },
    ActionRecord {
      action_id: "media_rate_down".into(),
      kind: ActionKind::Media,
      value: "rate_down".into(),
      label: "Speed −".into(),
    },
    ActionRecord {
      action_id: "media_rate_1x".into(),
      kind: ActionKind::Media,
      value: "rate_1x".into(),
      label: "Speed 1×".into(),
    },
    ActionRecord {
      action_id: "open_telegram".into(),
      kind: ActionKind::OpenApp,
      value: "ru.keepcoder.Telegram".into(),
      label: "Telegram".into(),
    },
    ActionRecord {
      action_id: "open_chatgpt".into(),
      kind: ActionKind::OpenApp,
      value: "com.openai.chat".into(),
      label: "ChatGPT".into(),
    },
    ActionRecord {
      action_id: "open_codex".into(),
      kind: ActionKind::OpenApp,
      value: "com.openai.codex".into(),
      label: "Codex".into(),
    },
    ActionRecord {
      action_id: "open_cursor".into(),
      kind: ActionKind::OpenApp,
      value: "com.todesktop.230313mzl4w4u92".into(),
      label: "Cursor".into(),
    },
    ActionRecord {
      action_id: "open_iterm".into(),
      kind: ActionKind::OpenApp,
      value: "com.googlecode.iterm2".into(),
      label: "iTerm".into(),
    },
    ActionRecord {
      action_id: "open_vscode".into(),
      kind: ActionKind::OpenApp,
      value: "com.microsoft.VSCode".into(),
      label: "VS Code".into(),
    },
    ActionRecord {
      action_id: "open_safari".into(),
      kind: ActionKind::OpenApp,
      value: "com.apple.Safari".into(),
      label: "Safari".into(),
    },
    ActionRecord {
      action_id: "open_slack".into(),
      kind: ActionKind::OpenApp,
      value: "com.tinyspeck.slackmacgap".into(),
      label: "Slack".into(),
    },
  ]
}
