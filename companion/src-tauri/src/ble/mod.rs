use std::collections::HashMap;
use std::time::Duration;

use btleplug::api::{Central, Characteristic, Manager as _, Peripheral as _, ScanFilter, WriteType};
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
  command_char: Option<Characteristic>,
  /// Device to reconnect to after link loss / app relaunch.
  last_id: Option<String>,
  /// User preference — when false, never auto-reconnect.
  auto_reconnect: bool,
  miss_checks: u8,
  /// Failures since last success — used for backoff.
  reconnect_failures: u8,
}

impl BleHub {
  pub fn new() -> Self {
    Self {
      adapter: None,
      peripherals: HashMap::new(),
      connected: None,
      connected_id: None,
      command_char: None,
      last_id: None,
      auto_reconnect: true,
      miss_checks: 0,
      reconnect_failures: 0,
    }
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

  /// Restore remembered device from disk (app launch).
  pub fn remember_device(&mut self, id: Option<String>) {
    self.last_id = id.filter(|s| !s.is_empty());
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

  /// Connect and forward every notify payload to `event_tx`.
  pub async fn connect(&mut self, id: &str, event_tx: EventTx) -> Result<(), BleError> {
    if self.connected.is_some() {
      self.clear_link().await;
    }
    if !self.peripherals.contains_key(id) {
      let _ = self.scan(Duration::from_secs(3)).await?;
    }
    let peri = self
      .peripherals
      .get(id)
      .cloned()
      .ok_or_else(|| BleError::Msg(format!("Device {id} not found — scan again")))?;

    peri
      .connect()
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;
    peri
      .discover_services()
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;

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
    let mut notifications = peri
      .notifications()
      .await
      .map_err(|e| BleError::Msg(e.to_string()))?;

    peri
      .subscribe(&event)
      .await
      .map_err(|e| BleError::Msg(format!("subscribe event: {e}")))?;
    info!("Subscribed to Event {}", EVENT_UUID);

    if let Some(ref st) = status {
      match peri.subscribe(st).await {
        Ok(()) => info!("Subscribed to Status {}", STATUS_UUID),
        Err(e) => warn!("subscribe status failed: {e}"),
      }
    }

    let tx = event_tx;
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
      warn!("GATT notification stream ended");
    });

    // Small delay then ping so we verify write + any hello/status path.
    tokio::time::sleep(Duration::from_millis(150)).await;
    self.command_char = Some(command);
    self.connected = Some(peri);
    self.connected_id = Some(id.to_string());
    self.last_id = Some(id.to_string());
    self.miss_checks = 0;
    self.reconnect_failures = 0;

    if let Err(e) = self
      .write_command(&serde_json::json!({"op": "ping"}))
      .await
    {
      warn!("ping after connect failed: {e}");
    } else {
      info!("Sent GATT ping");
    }

    info!("GATT connected to {id}");
    Ok(())
  }

  /// Drop the live link without forgetting the device (used before reconnect / link loss).
  async fn clear_link(&mut self) {
    if let Some(p) = self.connected.take() {
      let _ = p.disconnect().await;
    }
    self.connected_id = None;
    self.command_char = None;
    self.miss_checks = 0;
  }

  /// User-initiated disconnect — forget device so auto-reconnect stops.
  pub async fn disconnect(&mut self) -> Result<(), BleError> {
    self.clear_link().await;
    self.last_id = None;
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
    let peri = self
      .connected
      .as_ref()
      .ok_or_else(|| BleError::Msg("Not connected".into()))?;
    let ch = self
      .command_char
      .as_ref()
      .ok_or_else(|| BleError::Msg("No command characteristic".into()))?;
    let bytes = serde_json::to_vec(value).map_err(|e| BleError::Msg(e.to_string()))?;
    if let Err(e) = peri.write(ch, &bytes, WriteType::WithResponse).await {
      warn!("write WithResponse failed ({e}), trying WithoutResponse");
      peri
        .write(ch, &bytes, WriteType::WithoutResponse)
        .await
        .map_err(|e| BleError::Msg(e.to_string()))?;
    }
    Ok(())
  }

  pub async fn tick_reconnect(&mut self, event_tx: EventTx) -> Result<ReconnectTick, BleError> {
    if let Some(p) = &self.connected {
      if p.is_connected().await.unwrap_or(false) {
        self.miss_checks = 0;
        return Ok(ReconnectTick::Healthy);
      }
      self.miss_checks = self.miss_checks.saturating_add(1);
      if self.miss_checks < 3 {
        return Ok(ReconnectTick::Healthy);
      }
      warn!("GATT link lost (debounced)");
      self.clear_link().await;
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
        self.reconnect_failures = self.reconnect_failures.saturating_add(1);
        warn!("reconnect failed (#{}): {e}", self.reconnect_failures);
        Ok(ReconnectTick::AttemptFailed)
      }
    }
  }

  /// Sleep duration before next reconnect tick (backoff after failures).
  pub fn reconnect_interval(&self) -> Duration {
    if self.connected.is_some() {
      return Duration::from_secs(4);
    }
    match self.reconnect_failures {
      0..=1 => Duration::from_secs(3),
      2..=4 => Duration::from_secs(6),
      5..=8 => Duration::from_secs(12),
      _ => Duration::from_secs(20),
    }
  }
}
