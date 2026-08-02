# TouchDeck GATT Protocol v4

Firmware is a thin device. The companion owns actions.

**PROTOCOL_VERSION = 4** (additive extensions below; no version bump)

## Principles

- BLE GATT is the primary control channel.
- Wi-Fi is for portal, OTA, and icon upload only.
- Firmware does **not** emit HID media keys.
- Tile presses carry an `action_id`; the companion maps that to OpenApp / Media / Volume / Keyboard / etc.

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
| `action_id` | string | Stable id from grid / media controls (max 32 chars) |
| `t` | number | `millis()` on device |

### Media control `action_id`s (page 0)

| `action_id` | Companion |
|---|---|
| `media_play_pause` | Media play/pause key |
| `media_next` | Next track |
| `media_previous` | Previous track |
| `media_seek_fwd` | Seek +10s |
| `media_seek_back` | Seek −10s |
| `media_rate_up` / `media_rate_down` | Playback speed ± step (0.75×…2×) |
| `media_rate_1x` | Reset speed to 1× |
| `volume_up` / `volume_down` / `mute` | Existing volume actions |

Legacy fields (`id`, `target`) may still be present; companion prefers `action_id`.

## Command: host → board

JSON write to Command characteristic (max ~512 bytes):

```json
{"op":"volume","level":42,"muted":false}
{"op":"highlight","source":"cursor","on":true}
{"op":"brightness","pct":100}
{"op":"enter_ota"}
{"op":"ping"}
{"op":"now_playing","title":"Song","artist":"Artist","playing":true,"pos_ms":120000,"dur_ms":240000,"app":"Music","rate_x100":100}
{"op":"page","index":0}
{"op":"pages","count":2}
```

| `op` | Notes |
|---|---|
| `now_playing` | Update Media page UI (+ clock now-playing line when `playing`). Fields: `title`, `artist`, `playing`, `pos_ms`, `dur_ms`, `app`, `rate_x100` (100 = 1×). Strings truncated on device. Empty title = idle. |
| `page` | Switch pager to `index` (0 = media). Clamped to `page_count`. |
| `pages` | Set total page count ∈ {2,3,4}. Page 0 is always Media; count−1 shortcut grids. Persisted in grid profile. |
| `ping` | Keep-alive / RTT; board answers Status `pong`. |

## Status

```json
{"type":"hello","fw":"0.3.0","protocol":4,"gatt":true}
{"type":"pong"}
```

## Deck pages

- **Total pages:** 2–4 (default **2**).
- **Page 0:** fixed Media Now Playing UI (not tile-edited).
- **Pages 1…N-1:** shortcut grids (`open_app` / `open_url` / `keyboard` macros).

### Grid profile JSON

```json
{
  "rev": 1,
  "page_count": 2,
  "pages": [
    {
      "cols": 4,
      "rows": 2,
      "tiles": [
        {
          "id": "vscode",
          "label": "VS Code",
          "icon": "vscode",
          "color": "#007ACC",
          "action": "app",
          "action_id": "open_vscode"
        }
      ]
    }
  ]
}
```

- `pages.length` must equal `page_count - 1`.
- Legacy flat `{rev,cols,rows,tiles}` migrates to `page_count=2`, `pages[0]=…`.

Companion SQLite `actions` maps `action_id` → `{ kind, value, … }`.

## OTA

1. Companion writes `{"op":"enter_ota"}` (optional).
2. Firmware update over HTTP / ArduinoOTA / web installer — **not** over BLE.
