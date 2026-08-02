# BLE GATT & WebSocket protocol (TouchDeck)

Protocol version: **3**

Chi tiết logic approval notification: [`docs/approval-notifications.md`](approval-notifications.md).

---

## BLE GATT

Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| Command | `...0002...` | Write / WriteNR | JSON `{"op":"get_status"}` / `{"op":"restart"}` (legacy plain text still accepted) |
| Event | `...0003...` | Notify / Read | JSON events |
| Status | `...0004...` | Read / Notify | JSON status snapshot |
| Device Info | `...0005...` | Read | JSON model / fw / hw / protocol |

### GATT events

```json
{"type":"device_ready"}
{"type":"wifi_connected"}
{"type":"tile_press","id":"safari","t":12345,"target":{"kind":"bundle","value":"com.apple.Safari"}}
```

`target.kind` is `bundle` (macOS bundle ID) or `path` (absolute `.app` path).

---

## Companion transport: WebSocket (port 81)

`tile_press` is broadcast on `ws://<host>:81/` and **that is the channel the macOS companion uses**.

macOS reserves BLE HID peripherals. Once the board is paired as a keyboard, CoreBluetooth (and Chromium's Web Bluetooth) refuse GATT access to the same device, failing with `NetworkError: Unsupported device`. The GATT `tile_press` notification is kept for non-macOS clients that can use it.

**Approval notifications are WebSocket-only** — not mirrored on BLE GATT in v3.

### Deck → companion

Sent on connect and on events:

```json
{"type":"hello","model":"JC8048W550C","fw":"0.2.0","protocol":3}
```

```json
{"type":"tile_press","id":"cursor","t":12345,"target":{"kind":"bundle","value":"com.todesktop.230313mzl4w4u92"}}
```

```json
{"type":"media_press","action":"volume_up","handled":true,"t":12345}
```

```json
{"type":"pong"}
```

| `type` | When |
|---|---|
| `hello` | Immediately after WebSocket connect |
| `tile_press` | User taps an app tile |
| `media_press` | User taps a media tile (`handled` = BLE HID already sent) |
| `pong` | Reply to `{"op":"ping"}` |

### Companion → deck

#### Keepalive & volume (v2+)

```json
{"op":"ping"}
```

```json
{"op":"volume","level":65,"muted":false}
```

Board mirrors host volume in the header (`homeGridScreenSetVolume`).

#### Notifications (v3)

Push when Mac-side approval is detected; clear when resolved.

```json
{
  "op": "notification",
  "id": "cursor",
  "source": "cursor",
  "title": "Cursor",
  "body": "Cursor needs approval"
}
```

```json
{"op":"notification_clear","id":"cursor"}
```

```json
{"op":"notification_clear_all":true}
```

| Field | Required | Description |
|---|---|---|
| `op` | yes | `notification`, `notification_clear`, or `notification_clear_all` |
| `id` | for set/clear | Unique id; usually matches `source` (`cursor` / `codex`) |
| `source` | no | Used to highlight matching grid tile icon |
| `title` | no | Banner title (default: `"Approval"`) |
| `body` | no | Banner body (default: `"Waiting for approval"`) |

**Firmware handling** (`src/net/event_server.cpp`):

- `notification` → banner overlay + tile highlight + wake display
- `notification_clear` → remove one; `id` matches by id or source
- `notification_clear_all` → remove all banners and highlights

Max inbound JSON payload: **512 bytes**.

### Security

The companion launches apps via `/usr/bin/open` only — no shell commands. WebSocket has no auth in v3 (LAN trust model).

---

## HID

HID Consumer Control is separate (standard HID service) for `volume_up` / `volume_down` / `mute` / `play_pause` / `next` / `previous`.

---

## Version history

| Version | Changes |
|---|---|
| 1 | Initial GATT protocol |
| 2 | WebSocket `tile_press`, `volume` sync, `media_press` |
| 3 | `notification` / `notification_clear` / `notification_clear_all`; approval alerts on deck |
