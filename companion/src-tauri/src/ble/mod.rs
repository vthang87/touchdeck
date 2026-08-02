use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use btleplug::api::{
  Central, CentralEvent, Characteristic, Manager as _, Peripheral as _, ScanFilter, WriteType,
};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures::StreamExt;
use serde::{Deserialize, Serialize};
use thiserror::Error;
use tokio::sync::mpsc;
use tracing::{info, warn};
use uuid::Uuid;

pub const SERVICE_UUID: Uuid = Uuid::from_u128(0x6e400001_b5a3_f393_e0a9_e50e24dcca9e);
pub const COMMAND_UUID: Uuid = Uuid::from_u128(0x6e400002_b5a3_f393_e0a9_e50e24dcca9e);
pub const EVENT_UUID: Uuid = Uuid::from_u128(0x6e400003_b5a3_f393_e0a9_e50e24dcca9e);
pub const STATUS_UUID: Uuid = Uuid::from_u128(0x6e400004_b5a3_f393_e0a9_e50e24dcca9e);

#[derive(Debug, Error)]
pub enum BleError {
  #[error("{0}")]
  Msg(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscoveredDevice {
  pub id: String,
  pub name: String,
  pub rssi: Option<i16>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReconnectTick {
  /// Still connected — nothing to do.
  Healthy,
  /// Link just dropped; will attempt reconnect on next ticks.
  LinkLost,
  /// Successfully reconnected.
  Reconnected,
  /// Attempted reconnect and failed (will retry).
  AttemptFailed,
  /// Auto-reconnect off or no remembered device.
  Idle,
}

/// Incoming GATT notify payloads (UTF-8 JSON).
pub type EventTx = mpsc::UnboundedSender<String>;

pub struct BleHub {
  adapter: Option<Adapter>,
  peripherals: HashMap<String, Peripheral>,
  connected: Option<Peripheral>,
  connected_id: Option<String>,
  /// Advertised / last-known local name for the linked peripheral.
  connected_name: Option<String>,
  command_char: Option<Characteristic>,
  /// Device to reconnect to after link loss / app relaunch.
  last_id: Option<String>,
  last_name: Option<String>,
  /// User preference — when false, never auto-reconnect.
  auto_reconnect: bool,
  miss_checks: u8,
  /// Failures since last success — used for backoff.
  reconnect_failures: u8,
  /// Cleared when the GATT notify stream ends (btleplug event loop death).
  notify_alive: Arc<AtomicBool>,
  /// After a macOS disconnect, CB drops the Peripheral — must re-scan before connect.
  needs_fresh_scan: bool,
  /// Wakes the reconnect loop immediately when the notify stream dies.
  link_lost_tx: Option<mpsc::UnboundedSender<()>>,
}

impl BleHub {
  pub fn new() -> Self {
    Self {
      adapter: None,
      peripherals: HashMap::new(),
      connected: None,
      connected_id: None,
      connected_name: None,
      command_char: None,
      last_id: None,
      last_name: None,
      auto_reconnect: true,
      miss_checks: 0,
      reconnect_failures: 0,
      notify_alive: Arc::new(AtomicBool::new(false)),
      needs_fresh_scan: true,
      link_lost_tx: None,
    }
  }

  pub fn set_link_lost_signal(&mut self, tx: mpsc::UnboundedSender<()>) {
    self.link_lost_tx = Some(tx);
  }

  pub fn notify_alive(&self) -> bool {
    self.notify_alive.load(Ordering::SeqCst)
  }

  pub fn auto_reconnect(&self) -> bool {
    self.auto_reconnect
  }

  pub fn set_auto_reconnect(&mut self, on: bool) {
    self.auto_reconnect = on;
  }

  pub fn last_id(&self) -> Option<String> {
    self.last_id.clone()
  }

  pub fn last_name(&self) -> Option<String> {
    self.last_name.clone()
  }

  pub fn connected_name(&self) -> Option<String> {
    self.connected_name.clone()
  }

  /// Restore remembered device from disk (app launch).
  pub fn remember_device(&mut self, id: Option<String>, name: Option<String>) {
    self.last_id = id.filter(|s| !s.is_empty());
    self.last_name = name.filter(|s| !s.is_empty());
  }

  pub fn wants_reconnect(&self) -> bool {
    self.auto_reconnect && self.last_id.is_some() && self.connected.is_none()
  }

  async fn ensure_adapter(&mut self) -> Result<&Adapter, BleError> {
    if self.adapter.is_none() {
      let manager = Manager::new()
        .await
        .map_err(|e| BleError::Msg(e.to_string()))?;
      let adapters = manager
        .adapters()
        .await
        .map_err(|e| BleError::Msg(e.to_string()))?;
      let adapter = adapters
        .into_iter()
        .next()
        .ok_or_else(|| BleError::Msg("No Bluetooth adapter found".into()))?;
      self.adapter = Some(adapter);
    }
    Ok(self.adapter.as_ref().unwrap())
  }

  pub async fn scan(&mut self, timeout: Duration) -> Result<Vec<DiscoveredDevice>, BleError> {
    self.ensure_adapter().await?;
    let adapter = self.adapter.as_ref().unwrap().clone();
    adapter
      .start_scan(ScanFilter::default())
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;
    tokio::time::sleep(timeout).await;
    let _ = adapter.stop_scan().await;

    let mut out = Vec::new();
    self.peripherals.clear();
    let peris = adapter
      .peripherals()
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;
    for p in peris {
      let props = p
        .properties()
        .await
        .map_err(|e| BleError::Msg(e.to_string()))?
        .unwrap_or_default();
      let name = props
        .local_name
        .clone()
        .unwrap_or_else(|| "Unknown".into());
      let id = p.id().to_string();
      let has_service = props.services.iter().any(|u| *u == SERVICE_UUID);
      let looks_like_deck = name.to_lowercase().contains("touchdeck")
        || name.to_lowercase().starts_with("touch")
        || has_service;
      if !looks_like_deck {
        continue;
      }
      self.peripherals.insert(id.clone(), p);
      out.push(DiscoveredDevice {
        id,
        name,
        rssi: props.rssi,
      });
    }
    out.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(out)
  }

  async fn with_timeout<T, E>(
    dur: Duration,
    fut: impl std::future::Future<Output = Result<T, E>>,
    what: &str,
  ) -> Result<T, BleError>
  where
    E: std::fmt::Display,
  {
    match tokio::time::timeout(dur, fut).await {
      Ok(Ok(v)) => Ok(v),
      Ok(Err(e)) => Err(BleError::Msg(format!("{what}: {e}"))),
      Err(_) => Err(BleError::Msg(format!("{what}: timed out after {dur:?}"))),
    }
  }

  /// Connect and forward every notify payload to `event_tx`.
  pub async fn connect(&mut self, id: &str, event_tx: EventTx) -> Result<(), BleError> {
    if self.connected.is_some() {
      self.clear_link();
    }
    // On macOS, a disconnected Peripheral is removed from CoreBluetooth's map.
    // Reusing our cached handle makes reconnect hang — always re-scan after link loss.
    if self.needs_fresh_scan || !self.peripherals.contains_key(id) {
      self.peripherals.remove(id);
      info!("Scanning for {id} (fresh peripheral)…");
      let _ = self.scan(Duration::from_secs(3)).await?;
      self.needs_fresh_scan = false;
    }
    let peri = self
      .peripherals
      .get(id)
      .cloned()
      .ok_or_else(|| BleError::Msg(format!("Device {id} not found — scan again")))?;

    let name = peri
      .properties()
      .await
      .ok()
      .flatten()
      .and_then(|p| p.local_name)
      .filter(|n| !n.is_empty())
      .or_else(|| self.last_name.clone())
      .unwrap_or_else(|| "TouchDeck".into());

    Self::with_timeout(Duration::from_secs(10), peri.connect(), "peripheral.connect").await?;

    let setup = async {
      Self::with_timeout(
        Duration::from_secs(8),
        peri.discover_services(),
        "discover_services",
      )
      .await?;

      let chars: Vec<Characteristic> = peri.characteristics().into_iter().collect();
      info!(
        "GATT services discovered — {} characteristics: {}",
        chars.len(),
        chars
          .iter()
          .map(|c| c.uuid.to_string())
          .collect::<Vec<_>>()
          .join(", ")
      );

      let command = chars
        .iter()
        .find(|c| c.uuid == COMMAND_UUID)
        .cloned()
        .ok_or_else(|| BleError::Msg("Command characteristic missing".into()))?;
      let event = chars
        .iter()
        .find(|c| c.uuid == EVENT_UUID)
        .cloned()
        .ok_or_else(|| BleError::Msg("Event characteristic missing".into()))?;
      let status = chars.iter().find(|c| c.uuid == STATUS_UUID).cloned();

      // Stream BEFORE subscribe so CoreBluetooth does not drop early notifies.
      let notifications = Self::with_timeout(
        Duration::from_secs(5),
        peri.notifications(),
        "notifications()",
      )
      .await?;

      Self::with_timeout(
        Duration::from_secs(5),
        peri.subscribe(&event),
        "subscribe event",
      )
      .await?;
      info!("Subscribed to Event {}", EVENT_UUID);

      if let Some(ref st) = status {
        match tokio::time::timeout(Duration::from_secs(3), peri.subscribe(st)).await {
          Ok(Ok(())) => info!("Subscribed to Status {}", STATUS_UUID),
          Ok(Err(e)) => warn!("subscribe status failed: {e}"),
          Err(_) => warn!("subscribe status timed out"),
        }
      }

      Ok::<_, BleError>((command, notifications))
    };

    let (command, mut notifications) = match setup.await {
      Ok(v) => v,
      Err(e) => {
        warn!("GATT setup failed after connect — dropping: {e}");
        let p = peri.clone();
        tokio::spawn(async move {
          let _ = tokio::time::timeout(Duration::from_millis(500), p.disconnect()).await;
        });
        self.needs_fresh_scan = true;
        self.peripherals.remove(id);
        return Err(e);
      }
    };

    let tx = event_tx;
    self.notify_alive.store(true, Ordering::SeqCst);
    let alive = self.notify_alive.clone();
    let link_lost_tx = self.link_lost_tx.clone();
    tokio::spawn(async move {
      info!("GATT notification listener started");
      while let Some(n) = notifications.next().await {
        // NimBLE may include a trailing NUL; strip and accept lossy UTF-8.
        let mut raw_bytes = n.value;
        while raw_bytes.last() == Some(&0) {
          raw_bytes.pop();
        }
        if raw_bytes.is_empty() {
          continue;
        }
        if raw_bytes.len() <= 8 {
          let hex: String = raw_bytes
            .iter()
            .map(|b| format!("{b:02x}"))
            .collect::<Vec<_>>()
            .join(" ");
          warn!("GATT short notify uuid={} hex=[{hex}]", n.uuid);
        }
        let raw = String::from_utf8_lossy(&raw_bytes).into_owned();
        info!("GATT notify [{}] {}", n.uuid, raw);
        if tx.send(raw).is_err() {
          warn!("GATT event channel closed — listener exiting");
          break;
        }
      }
      alive.store(false, Ordering::SeqCst);
      if let Some(t) = link_lost_tx {
        let _ = t.send(());
      }
      warn!("GATT notification stream ended — will re-scan on reconnect");
    });

    // Small delay then ping so we verify write + any hello/status path.
    tokio::time::sleep(Duration::from_millis(150)).await;
    self.command_char = Some(command);
    self.connected = Some(peri);
    self.connected_id = Some(id.to_string());
    self.connected_name = Some(name.clone());
    self.last_id = Some(id.to_string());
    self.last_name = Some(name);
    self.miss_checks = 0;
    self.reconnect_failures = 0;

    if let Err(e) = self
      .write_command_fast(&serde_json::json!({"op": "ping"}))
      .await
    {
      warn!("ping after connect failed: {e}");
    } else {
      info!("Sent GATT ping");
    }

    info!("GATT connected to {id}");
    Ok(())
  }

  /// Public escape hatch when a reconnect tick itself times out while holding the mutex.
  pub fn force_clear_link(&mut self) {
    self.clear_link();
  }

  /// Drop the live link without forgetting the device (used before reconnect / link loss).
  ///
  /// Must never block on CoreBluetooth: after `DeviceDisconnected`, `Peripheral::disconnect`
  /// often hangs forever and would lock the BLE mutex (and UI) indefinitely.
  fn clear_link(&mut self) {
    self.notify_alive.store(false, Ordering::SeqCst);
    self.needs_fresh_scan = true;
    let id = self.connected_id.take();
    let peri = self.connected.take();
    if let Some(ref id) = id {
      // Drop cached handle — CoreBluetooth already removed it on disconnect.
      self.peripherals.remove(id);
    }
    self.connected_name = None;
    self.command_char = None;
    self.miss_checks = 0;
    if let Some(p) = peri {
      tokio::spawn(async move {
        match tokio::time::timeout(Duration::from_millis(500), p.disconnect()).await {
          Ok(Ok(())) => info!("Background disconnect finished"),
          Ok(Err(e)) => warn!("Background disconnect: {e}"),
          Err(_) => warn!("Background disconnect timed out — peripheral dropped"),
        }
      });
    }
    info!("BLE link cleared (ready to re-scan)");
  }

  /// User-initiated disconnect — forget device so auto-reconnect stops.
  pub async fn disconnect(&mut self) -> Result<(), BleError> {
    self.clear_link();
    self.last_id = None;
    self.last_name = None;
    self.reconnect_failures = 0;
    Ok(())
  }

  pub fn is_connected(&self) -> bool {
    self.connected.is_some()
  }

  pub fn connected_id(&self) -> Option<String> {
    self.connected_id.clone()
  }

  pub async fn write_command(&self, value: &serde_json::Value) -> Result<(), BleError> {
    self.write_command_ex(value, true).await
  }

  /// Fire-and-forget write (Now Playing updates). Avoids WithResponse stalls.
  pub async fn write_command_fast(&self, value: &serde_json::Value) -> Result<(), BleError> {
    self.write_command_ex(value, false).await
  }

  async fn write_command_ex(&self, value: &serde_json::Value, with_response: bool) -> Result<(), BleError> {
    let peri = self
      .connected
      .as_ref()
      .ok_or_else(|| BleError::Msg("Not connected".into()))?;
    let ch = self
      .command_char
      .as_ref()
      .ok_or_else(|| BleError::Msg("No command characteristic".into()))?;
    let bytes = serde_json::to_vec(value).map_err(|e| BleError::Msg(e.to_string()))?;
    if with_response {
      match tokio::time::timeout(
        Duration::from_secs(2),
        peri.write(ch, &bytes, WriteType::WithResponse),
      )
      .await
      {
        Ok(Ok(())) => {}
        Ok(Err(e)) => {
          warn!("write WithResponse failed ({e}), trying WithoutResponse");
          tokio::time::timeout(
            Duration::from_secs(2),
            peri.write(ch, &bytes, WriteType::WithoutResponse),
          )
          .await
          .map_err(|_| BleError::Msg("write WithoutResponse timed out".into()))?
          .map_err(|e| BleError::Msg(e.to_string()))?;
        }
        Err(_) => {
          warn!("write WithResponse timed out, trying WithoutResponse");
          tokio::time::timeout(
            Duration::from_secs(2),
            peri.write(ch, &bytes, WriteType::WithoutResponse),
          )
          .await
          .map_err(|_| BleError::Msg("write WithoutResponse timed out".into()))?
          .map_err(|e| BleError::Msg(e.to_string()))?;
        }
      }
    } else {
      tokio::time::timeout(
        Duration::from_secs(2),
        peri.write(ch, &bytes, WriteType::WithoutResponse),
      )
      .await
      .map_err(|_| BleError::Msg("write timed out".into()))?
      .map_err(|e| BleError::Msg(e.to_string()))?;
    }
    Ok(())
  }

  pub async fn tick_reconnect(&mut self, event_tx: EventTx) -> Result<ReconnectTick, BleError> {
    if self.connected.is_some() && !self.notify_alive.load(Ordering::SeqCst) {
      warn!("GATT notify stream dead — forcing reconnect");
      self.clear_link();
      if !self.auto_reconnect || self.last_id.is_none() {
        return Ok(ReconnectTick::LinkLost);
      }
      // Fall through to reconnect attempt below.
    }
    if let Some(p) = &self.connected {
      // is_connected can also hang on a dead CB handle — bound it.
      let still = match tokio::time::timeout(Duration::from_millis(800), p.is_connected()).await {
        Ok(Ok(v)) => v,
        Ok(Err(_)) => false,
        Err(_) => {
          warn!("is_connected timed out — treating as link lost");
          false
        }
      };
      if still {
        self.miss_checks = 0;
        return Ok(ReconnectTick::Healthy);
      }
      self.miss_checks = self.miss_checks.saturating_add(1);
      if self.miss_checks < 2 {
        return Ok(ReconnectTick::Healthy);
      }
      warn!("GATT link lost (debounced)");
      self.clear_link();
      if !self.auto_reconnect || self.last_id.is_none() {
        return Ok(ReconnectTick::LinkLost);
      }
      // Fall through to reconnect attempt immediately.
    }

    if !self.auto_reconnect {
      return Ok(ReconnectTick::Idle);
    }
    let Some(id) = self.last_id.clone() else {
      return Ok(ReconnectTick::Idle);
    };

    info!("Attempting reconnect to {id}");
    match self.connect(&id, event_tx).await {
      Ok(()) => {
        self.reconnect_failures = 0;
        Ok(ReconnectTick::Reconnected)
      }
      Err(e) => {
        // Ensure we don't keep a half-open / hung handle.
        self.clear_link();
        self.reconnect_failures = self.reconnect_failures.saturating_add(1);
        warn!("reconnect failed (#{}): {e}", self.reconnect_failures);
        Ok(ReconnectTick::AttemptFailed)
      }
    }
  }

  /// Sleep duration before next reconnect tick (backoff after failures).
  pub fn reconnect_interval(&self) -> Duration {
    if self.connected.is_some() {
      if !self.notify_alive.load(Ordering::SeqCst) {
        return Duration::from_millis(400);
      }
      return Duration::from_secs(3);
    }
    match self.reconnect_failures {
      0..=1 => Duration::from_secs(2),
      2..=4 => Duration::from_secs(4),
      5..=8 => Duration::from_secs(8),
      _ => Duration::from_secs(15),
    }
  }

  /// Watch adapter disconnect events and wake the reconnect loop.
  pub async fn spawn_disconnect_watcher(&mut self) -> Result<(), BleError> {
    let adapter = self.ensure_adapter().await?.clone();
    let mut events = adapter
      .events()
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;
    let alive = self.notify_alive.clone();
    let link_lost_tx = self.link_lost_tx.clone();
    tokio::spawn(async move {
      while let Some(ev) = events.next().await {
        if let CentralEvent::DeviceDisconnected(id) = ev {
          warn!("Adapter DeviceDisconnected: {id}");
          alive.store(false, Ordering::SeqCst);
          if let Some(tx) = &link_lost_tx {
            let _ = tx.send(());
          }
        }
      }
      warn!("Adapter event stream ended");
    });
    Ok(())
  }

  /// GATT keep-alive so macOS/NimBLE are less likely to idle-drop the link.
  pub async fn ping(&self) -> Result<(), BleError> {
    self.write_command_fast(&serde_json::json!({"op": "ping"}))
      .await
  }
}
