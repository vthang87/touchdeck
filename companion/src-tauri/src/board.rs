//! HTTP client for the board Wi‑Fi portal (`/api/settings`, `/api/grid`, …).

use serde::{Deserialize, Serialize};
use thiserror::Error;

const ALLOWED_PATHS: &[&str] = &[
  "/api/grid",
  "/api/grid/reset",
  "/api/icons",
  "/api/settings",
];

#[derive(Debug, Error)]
pub enum BoardError {
  #[error("{0}")]
  Msg(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardResponse {
  pub ok: bool,
  pub status: u16,
  pub text: String,
  #[serde(skip_serializing_if = "Option::is_none")]
  pub error: Option<String>,
}

fn valid_host(host: &str) -> bool {
  let h = host.trim();
  !h.is_empty()
    && h.len() <= 253
    && h
      .chars()
      .all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '-' || c == ':')
}

fn allowed_path(path: &str) -> bool {
  ALLOWED_PATHS.contains(&path)
}

pub async fn request(
  host: &str,
  path: &str,
  method: &str,
  body: &str,
) -> Result<BoardResponse, BoardError> {
  let host = host.trim();
  if !valid_host(host) {
    return Ok(BoardResponse {
      ok: false,
      status: 400,
      text: String::new(),
      error: Some("Invalid board host".into()),
    });
  }
  if !allowed_path(path) {
    return Ok(BoardResponse {
      ok: false,
      status: 400,
      text: String::new(),
      error: Some("Invalid board API path".into()),
    });
  }
  let method = method.to_uppercase();
  if method != "GET" && method != "POST" {
    return Ok(BoardResponse {
      ok: false,
      status: 405,
      text: String::new(),
      error: Some("Unsupported method".into()),
    });
  }

  let url = format!("http://{host}{path}");
  let client = reqwest::Client::builder()
    .timeout(std::time::Duration::from_secs(12))
    .build()
    .map_err(|e| BoardError::Msg(e.to_string()))?;

  let builder = match method.as_str() {
    "POST" => client
      .post(&url)
      .header("Content-Type", "application/x-www-form-urlencoded")
      .body(body.to_string()),
    _ => client.get(&url),
  };

  match builder.send().await {
    Ok(resp) => {
      let status = resp.status().as_u16();
      let ok = resp.status().is_success();
      let text = resp.text().await.unwrap_or_default();
      Ok(BoardResponse {
        ok,
        status,
        text,
        error: None,
      })
    }
    Err(e) => Ok(BoardResponse {
      ok: false,
      status: 0,
      text: String::new(),
      error: Some(e.to_string()),
    }),
  }
}
