# TouchDeck GATT Protocol v4

Firmware is a thin device. The companion owns actions.

**PROTOCOL_VERSION = 4**

## Principles

- BLE GATT is the primary control channel.
- Wi-Fi is for portal, OTA, and icon upload only.
- Firmware does **not** emit HID media keys.
- Tile presses carry an `action_id`; the companion maps that to OpenApp / Media / Volume / etc.

## UUIDs

Same base as prior TouchDeck GATT (NimBLE):

| Role | UUID |
|---|---|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Command (host → board, write) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Event (board → host, notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| Status (board → host, notify/read) | `6e400004-b5a3-f393-e0a9-e50e24dcca9e` |

## Event: tile_press

Board → companion (Event characteristic):

```json
{"event":"tile_press","action_id":"open_vscode","t":12345}
```

| Field | Type | Notes |
|---|---|---|
| `event` | string | Always `tile_press` |
| `action_id` | string | Stable id from grid config (max 32 chars) |
| `t` | number | `millis()` on device |

Legacy fields (`id`, `target`) may still be present during migration; companion prefers `action_id`.

## Command: host → board

JSON write to Command characteristic (max ~512 bytes):

```json
{"op":"volume","level":42,"muted":false}
{"op":"highlight","source":"cursor","on":true}
{"op":"brightness","pct":100}
{"op":"enter_ota"}
{"op":"ping"}
```

## Status

Board may notify:

```json
{"type":"hello","fw":"0.3.0","protocol":4,"gatt":true}
{"type":"pong"}
```

## Grid config

Each tile stores:

- `id`, `label`, `icon`, `color`, `action` (UI kind hint: `app` / `volume_up` / …)
- `action_id` — required for companion routing when `action` needs the host

Example:

```json
{
  "id": "vscode",
  "label": "VS Code",
  "icon": "vscode",
  "color": "#007ACC",
  "action": "app",
  "action_id": "open_vscode"
}
```

Companion SQLite `actions` table maps `action_id` → `{ kind, value, … }`.

## OTA

1. Companion writes `{"op":"enter_ota"}` (optional; board already has Wi-Fi when provisioned).
2. Firmware update over HTTP / ArduinoOTA / web installer — **not** over BLE.
