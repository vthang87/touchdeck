pub mod action;
pub mod ble;
pub mod board;
pub mod permissions;
pub mod platform;
pub mod storage;
pub mod tray;
pub mod virtual_keyboard;

use std::sync::Arc;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, State};
use tokio::sync::{mpsc, Mutex};
use tracing::{error, info, warn};

use crate::action::engine::ActionEngine;
use crate::ble::{BleHub, DiscoveredDevice, ReconnectTick};
use crate::board::BoardResponse;
use crate::permissions::PermissionStatus;
use crate::storage::{ActionRecord, Store};

#[derive(Clone)]
pub struct AppState {
  pub store: Arc<Mutex<Store>>,
  pub ble: Arc<Mutex<BleHub>>,
  pub engine: Arc<ActionEngine>,
  /// Cloneable sender — each BLE connect feeds notifies into this bus.
  pub event_tx: mpsc::UnboundedSender<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LogLine {
  pub t: String,
  pub level: String,
  pub message: String,
}

fn emit_log(app: &AppHandle, level: &str, message: impl Into<String>) {
  let line = LogLine {
    t: chrono::Local::now().format("%H:%M:%S").to_string(),
    level: level.to_string(),
    message: message.into(),
  };
  let _ = app.emit("companion-log", &line);
}

#[tauri::command]
async fn list_actions(state: State<'_, AppState>) -> Result<Vec<ActionRecord>, String> {
  let store = state.store.lock().await;
  store.list_actions().map_err(|e| e.to_string())
}

#[tauri::command]
async fn upsert_action(state: State<'_, AppState>, action: ActionRecord) -> Result<(), String> {
  let store = state.store.lock().await;
  store.upsert_action(&action).map_err(|e| e.to_string())
}

#[tauri::command]
async fn delete_action(state: State<'_, AppState>, action_id: String) -> Result<(), String> {
  let store = state.store.lock().await;
  store.delete_action(&action_id).map_err(|e| e.to_string())
}

#[tauri::command]
async fn ble_scan(state: State<'_, AppState>, app: AppHandle) -> Result<Vec<DiscoveredDevice>, String> {
  emit_log(&app, "info", "Scanning for TouchDeck…");
  let mut ble = state.ble.lock().await;
  match ble.scan(std::time::Duration::from_secs(5)).await {
    Ok(list) => {
      crate::permissions::mark_bluetooth_used();
      {
        let store = state.store.lock().await;
        let _ = store.set_setting("ble_permission_ok", "1");
      }
      emit_log(&app, "info", format!("Found {} device(s)", list.len()));
      Ok(list)
    }
    Err(e) => {
      emit_log(&app, "error", e.to_string());
      Err(e.to_string())
    }
  }
}

#[tauri::command]
fn permissions_status() -> PermissionStatus {
  crate::permissions::status()
}

#[tauri::command]
fn request_accessibility(app: AppHandle) -> Result<bool, String> {
  emit_log(&app, "info", "Requesting Accessibility permission…");
  let ok = crate::permissions::request_accessibility();
  if ok {
    emit_log(&app, "info", "Accessibility granted");
  } else {
    emit_log(
      &app,
      "warn",
      "Accessibility not granted yet — check System Settings (see alert).",
    );
  }
  Ok(ok)
}

#[tauri::command]
fn request_bluetooth(app: AppHandle) -> Result<(), String> {
  emit_log(&app, "info", "Requesting Bluetooth permission…");
  crate::permissions::request_bluetooth()?;
  emit_log(
    &app,
    "info",
    "After enabling Bluetooth in Settings, press Scan BLE.",
  );
  Ok(())
}

#[tauri::command]
fn open_permission_settings(kind: String) -> Result<(), String> {
  match kind.as_str() {
    "accessibility" => crate::permissions::open_accessibility_settings(),
    "bluetooth" => crate::permissions::open_bluetooth_settings(),
    _ => Err(format!("unknown permission kind: {kind}")),
  }
}

#[tauri::command]
async fn ble_connect(state: State<'_, AppState>, app: AppHandle, id: String) -> Result<(), String> {
  emit_log(&app, "info", format!("Connecting to {id}…"));
  let tx = state.event_tx.clone();
  let mut ble = state.ble.lock().await;
  ble.connect(&id, tx).await.map_err(|e| {
    emit_log(&app, "error", e.to_string());
    e.to_string()
  })?;
  drop(ble);
  {
    let store = state.store.lock().await;
    let _ = store.set_setting("ble_last_id", &id);
    let _ = store.set_setting("ble_permission_ok", "1");
  }
  crate::permissions::mark_bluetooth_used();
  emit_log(&app, "info", "Connected — listening for tile_press");
  let _ = app.emit(
    "ble-status",
    serde_json::json!({"connected": true, "id": id, "reconnecting": false}),
  );
  Ok(())
}

#[tauri::command]
async fn ble_disconnect(state: State<'_, AppState>, app: AppHandle) -> Result<(), String> {
  let mut ble = state.ble.lock().await;
  ble.disconnect().await.map_err(|e| e.to_string())?;
  drop(ble);
  {
    let store = state.store.lock().await;
    let _ = store.delete_setting("ble_last_id");
  }
  emit_log(&app, "info", "Disconnected — auto-reconnect cleared");
  let _ = app.emit(
    "ble-status",
    serde_json::json!({"connected": false, "reconnecting": false}),
  );
  Ok(())
}

#[tauri::command]
async fn ble_status(state: State<'_, AppState>) -> Result<serde_json::Value, String> {
  let ble = state.ble.lock().await;
  Ok(serde_json::json!({
    "connected": ble.is_connected(),
    "id": ble.connected_id(),
    "lastId": ble.last_id(),
    "autoReconnect": ble.auto_reconnect(),
    "reconnecting": ble.wants_reconnect(),
  }))
}

#[tauri::command]
async fn get_auto_reconnect(state: State<'_, AppState>) -> Result<bool, String> {
  let ble = state.ble.lock().await;
  Ok(ble.auto_reconnect())
}

#[tauri::command]
async fn set_auto_reconnect(
  state: State<'_, AppState>,
  app: AppHandle,
  enabled: bool,
) -> Result<(), String> {
  {
    let mut ble = state.ble.lock().await;
    ble.set_auto_reconnect(enabled);
  }
  {
    let store = state.store.lock().await;
    store
      .set_setting("ble_auto_reconnect", if enabled { "1" } else { "0" })
      .map_err(|e| e.to_string())?;
  }
  emit_log(
    &app,
    "info",
    if enabled {
      "Auto-reconnect enabled"
    } else {
      "Auto-reconnect disabled"
    },
  );
  let ble = state.ble.lock().await;
  let _ = app.emit(
    "ble-status",
    serde_json::json!({
      "connected": ble.is_connected(),
      "id": ble.connected_id(),
      "lastId": ble.last_id(),
      "autoReconnect": enabled,
      "reconnecting": ble.wants_reconnect(),
    }),
  );
  Ok(())
}

#[tauri::command]
async fn get_board_host(state: State<'_, AppState>) -> Result<String, String> {
  let store = state.store.lock().await;
  Ok(
    store
      .get_setting("board_host")
      .map_err(|e| e.to_string())?
      .unwrap_or_else(|| "touchdeck.local".into()),
  )
}

#[tauri::command]
async fn set_board_host(state: State<'_, AppState>, host: String) -> Result<(), String> {
  let host = host.trim().to_string();
  if host.is_empty() {
    return Err("Board host required".into());
  }
  let store = state.store.lock().await;
  store
    .set_setting("board_host", &host)
    .map_err(|e| e.to_string())
}

#[tauri::command]
async fn board_request(
  state: State<'_, AppState>,
  app: AppHandle,
  path: String,
  method: Option<String>,
  body: Option<String>,
  host: Option<String>,
) -> Result<BoardResponse, String> {
  let host = if let Some(h) = host.filter(|s| !s.trim().is_empty()) {
    h
  } else {
    let store = state.store.lock().await;
    store
      .get_setting("board_host")
      .map_err(|e| e.to_string())?
      .unwrap_or_else(|| "touchdeck.local".into())
  };
  let method = method.unwrap_or_else(|| "GET".into());
  let body = body.unwrap_or_default();
  emit_log(
    &app,
    "info",
    format!("Board {method} http://{host}{path}"),
  );
  let resp = crate::board::request(&host, &path, &method, &body)
    .await
    .map_err(|e| e.to_string())?;
  if let Some(ref err) = resp.error {
    emit_log(&app, "error", format!("Board request failed: {err}"));
  } else if !resp.ok {
    emit_log(
      &app,
      "warn",
      format!("Board HTTP {} — {}", resp.status, resp.text.chars().take(120).collect::<String>()),
    );
  }
  Ok(resp)
}

#[tauri::command]
async fn push_volume(state: State<'_, AppState>, level: u8, muted: bool) -> Result<(), String> {
  let ble = state.ble.lock().await;
  ble
    .write_command(&serde_json::json!({"op":"volume","level":level,"muted":muted}))
    .await
    .map_err(|e| e.to_string())
}

/// Inject a virtual-keyboard event on the host (for Connect UI testing + future deck maps).
#[tauri::command]
fn vk_inject(app: AppHandle, kind: String, value: String) -> Result<(), String> {
  emit_log(&app, "info", format!("virtual keyboard → {kind}:{value}"));
  match kind.as_str() {
    "media" => crate::virtual_keyboard::media(&value).or_else(|e| {
      warn!("enigo media: {e}");
      crate::platform::media_key(&value)
    }),
    "keyboard" => crate::virtual_keyboard::keyboard(&value).or_else(|e| {
      warn!("enigo keyboard: {e}");
      crate::platform::keyboard(&value)
    }),
    "volume" => {
      let op = if value == "down" || value == "volume_down" {
        "volume_down"
      } else if value == "mute" {
        "mute"
      } else {
        "volume_up"
      };
      crate::virtual_keyboard::media(op).or_else(|_| {
        if op == "mute" {
          crate::platform::toggle_mute().map(|_| ())
        } else {
          let delta = if op == "volume_down" { -3 } else { 3 };
          crate::platform::adjust_volume(delta).map(|_| ())
        }
      })
    }
    _ => Err(format!("unknown vk kind: {kind}")),
  }
}

fn resolve_action(store: &Store, action_id: &str) -> Result<Option<ActionRecord>, String> {
  if let Some(r) = store.get_action(action_id).map_err(|e| e.to_string())? {
    return Ok(Some(r));
  }
  // Legacy grids often send tile id ("cursor") instead of "open_cursor".
  if !action_id.starts_with("open_")
    && action_id != "volume_up"
    && action_id != "volume_down"
    && action_id != "mute"
    && action_id != "play_pause"
    && action_id != "next"
    && action_id != "previous"
  {
    let alt = format!("open_{action_id}");
    if let Some(r) = store.get_action(&alt).map_err(|e| e.to_string())? {
      return Ok(Some(r));
    }
  }
  // open_gpt ↔ open_chatgpt
  if action_id == "open_gpt" {
    if let Some(r) = store.get_action("open_chatgpt").map_err(|e| e.to_string())? {
      return Ok(Some(r));
    }
  }
  Ok(None)
}

async fn handle_tile_press(app: AppHandle, state: AppState, action_id: String) {
  emit_log(&app, "info", format!("tile_press → {action_id}"));
  let record = {
    let store = state.store.lock().await;
    match resolve_action(&store, &action_id) {
      Ok(Some(r)) => r,
      Ok(None) => {
        emit_log(
          &app,
          "warn",
          format!("No mapping for action_id={action_id} — add it in the Profile tab"),
        );
        return;
      }
      Err(e) => {
        emit_log(&app, "error", e);
        return;
      }
    }
  };

  match state.engine.execute(&record).await {
    Ok(Some(vol)) => {
      emit_log(
        &app,
        "info",
        format!(
          "volume now {}%{}",
          vol.level,
          if vol.muted { " (muted)" } else { "" }
        ),
      );
      let ble = state.ble.lock().await;
      if let Err(e) = ble
        .write_command(&serde_json::json!({
          "op": "volume",
          "level": vol.level,
          "muted": vol.muted
        }))
        .await
      {
        emit_log(&app, "warn", format!("push volume to board failed: {e}"));
      }
    }
    Ok(None) => {
      emit_log(&app, "info", format!("executed {}", record.action_id));
    }
    Err(e) => {
      emit_log(&app, "error", format!("action failed: {e}"));
    }
  }
}

fn spawn_event_bus(app: AppHandle, state: AppState, mut rx: mpsc::UnboundedReceiver<String>) {
  tauri::async_runtime::spawn(async move {
    while let Some(ev) = rx.recv().await {
      emit_log(&app, "debug", format!("GATT: {ev}"));
      if let Some(action_id) = parse_tile_press(&ev) {
        handle_tile_press(app.clone(), state.clone(), action_id).await;
      }
    }
    warn!("event bus ended");
  });
}

fn parse_tile_press(json: &str) -> Option<String> {
  let v: serde_json::Value = serde_json::from_str(json).ok()?;
  let event = v.get("event").and_then(|x| x.as_str()).unwrap_or("");
  let ty = v.get("type").and_then(|x| x.as_str()).unwrap_or("");
  if event == "tile_press" || ty == "tile_press" {
    if let Some(id) = v.get("action_id").and_then(|x| x.as_str()).filter(|s| !s.is_empty()) {
      return Some(id.to_string());
    }
    // Fallback: legacy id field
    if let Some(id) = v.get("id").and_then(|x| x.as_str()).filter(|s| !s.is_empty()) {
      return Some(id.to_string());
    }
  }
  None
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
  tracing_subscriber::fmt()
    .with_env_filter("info")
    .with_target(false)
    .init();

  let store = Store::open_default().expect("open sqlite store");
  store.seed_defaults().expect("seed defaults");
  let store = Arc::new(Mutex::new(store));
  let ble = Arc::new(Mutex::new(BleHub::new()));
  let engine = Arc::new(ActionEngine::new());
  let (event_tx, event_rx) = mpsc::unbounded_channel::<String>();
  let state = AppState {
    store: store.clone(),
    ble: ble.clone(),
    engine: engine.clone(),
    event_tx: event_tx.clone(),
  };

  tauri::Builder::default()
    .plugin(tauri_plugin_opener::init())
    .manage(state.clone())
    .setup(move |app| {
      tray::setup_tray(app.handle())?;
      spawn_event_bus(app.handle().clone(), state.clone(), event_rx);

      let app_for_perms = app.handle().clone();
      std::thread::spawn(move || {
        std::thread::sleep(std::time::Duration::from_millis(900));
        if !crate::permissions::accessibility_trusted() {
          emit_log(&app_for_perms, "warn", "Accessibility missing — showing prompt");
          let _ = crate::permissions::request_accessibility();
        }
      });

      let app_h = app.handle().clone();
      let ble_h = ble.clone();
      let tx_h = event_tx.clone();
      let store_h = store.clone();

      // Restore remembered device + auto-reconnect preference.
      {
        let st = store_h.blocking_lock();
        let auto = st
          .get_setting("ble_auto_reconnect")
          .ok()
          .flatten()
          .map(|v| v != "0" && v != "false")
          .unwrap_or(true);
        let last = st.get_setting("ble_last_id").ok().flatten();
        let ble_ok = st
          .get_setting("ble_permission_ok")
          .ok()
          .flatten()
          .map(|v| v == "1" || v == "true")
          .unwrap_or(false)
          || last.is_some();
        drop(st);
        crate::permissions::hydrate_bluetooth_ready(ble_ok);
        let mut hub = ble_h.blocking_lock();
        hub.set_auto_reconnect(auto);
        hub.remember_device(last.clone());
        if let Some(ref id) = last {
          if auto {
            emit_log(
              app.handle(),
              "info",
              format!("Will auto-reconnect to {id}"),
            );
          }
        }
      }

      tauri::async_runtime::spawn(async move {
        // First attempt soon after launch.
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
        loop {
          let interval = {
            let hub = ble_h.lock().await;
            hub.reconnect_interval()
          };
          tokio::time::sleep(interval).await;
          let mut hub = ble_h.lock().await;
          let was_connected = hub.is_connected();
          match hub.tick_reconnect(tx_h.clone()).await {
            Ok(ReconnectTick::Healthy) => {}
            Ok(ReconnectTick::Idle) => {}
            Ok(ReconnectTick::LinkLost) => {
              emit_log(&app_h, "warn", "BLE link lost");
              let _ = app_h.emit(
                "ble-status",
                serde_json::json!({
                  "connected": false,
                  "id": hub.last_id(),
                  "lastId": hub.last_id(),
                  "autoReconnect": hub.auto_reconnect(),
                  "reconnecting": hub.wants_reconnect(),
                }),
              );
            }
            Ok(ReconnectTick::Reconnected) => {
              let id = hub.connected_id();
              emit_log(
                &app_h,
                "info",
                format!("Reconnected to {}", id.clone().unwrap_or_default()),
              );
              if let Some(ref id) = id {
                let st = store_h.lock().await;
                let _ = st.set_setting("ble_last_id", id);
              }
              let _ = app_h.emit(
                "ble-status",
                serde_json::json!({
                  "connected": true,
                  "id": id,
                  "lastId": hub.last_id(),
                  "autoReconnect": hub.auto_reconnect(),
                  "reconnecting": false,
                }),
              );
            }
            Ok(ReconnectTick::AttemptFailed) => {
              if was_connected {
                emit_log(&app_h, "warn", "BLE link lost — reconnecting…");
              } else {
                emit_log(&app_h, "info", "Auto-reconnect attempt failed — retrying…");
              }
              let _ = app_h.emit(
                "ble-status",
                serde_json::json!({
                  "connected": false,
                  "id": hub.last_id(),
                  "lastId": hub.last_id(),
                  "autoReconnect": hub.auto_reconnect(),
                  "reconnecting": hub.wants_reconnect(),
                }),
              );
            }
            Err(e) => error!("reconnect: {e}"),
          }
        }
      });
      info!("TouchDeck Companion ready");
      let perms = crate::permissions::status();
      info!(
        "permissions: accessibility={} bluetooth_ready={} path={}",
        perms.accessibility, perms.bluetooth_ready, perms.binary_path
      );
      emit_log(
        app.handle(),
        if perms.accessibility { "info" } else { "warn" },
        format!(
          "Accessibility: {}",
          if perms.accessibility { "OK ✓" } else { "NOT GRANTED ✗" }
        ),
      );
      Ok(())
    })
    .on_window_event(|window, event| {
      if let tauri::WindowEvent::CloseRequested { api, .. } = event {
        api.prevent_close();
        let _ = window.hide();
      }
    })
    .invoke_handler(tauri::generate_handler![
      list_actions,
      upsert_action,
      delete_action,
      ble_scan,
      ble_connect,
      ble_disconnect,
      ble_status,
      get_auto_reconnect,
      set_auto_reconnect,
      get_board_host,
      set_board_host,
      board_request,
      push_volume,
      vk_inject,
      permissions_status,
      request_accessibility,
      request_bluetooth,
      open_permission_settings
    ])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
