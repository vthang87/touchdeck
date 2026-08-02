# TouchDeck Companion (Tauri)

**Primary** macOS companion — Rust + Tauri 2 + React over **BLE GATT** (protocol v4).

Legacy Electron + WebSocket build: [`../archive/companion-electron/`](../archive/companion-electron/).

## Dev

```bash
pnpm install
pnpm tauri dev
```

## Package

```bash
pnpm run dist
# → src-tauri/target/release/bundle/dmg/*.dmg
```

## Permissions (macOS)

- **Bluetooth** — scan/connect to TouchDeck
- **Accessibility** — media / keyboard / volume / seek simulation
- **Automation** (optional) — Music / Spotify AppleScript fallback; Safari may need *Allow JavaScript from Apple Events* for playback rate

## Deck pages & Now Playing

While connected, companion pushes GATT `now_playing` (~1–3s) from the system Now Playing session (JXA `MRNowPlayingRequest`). Media controls use seeded Profile action ids:

| `action_id` | Action |
|---|---|
| `media_play_pause` / `media_next` / `media_previous` | Transport |
| `media_seek_fwd` / `media_seek_back` | ±10s |
| `media_rate_up` / `media_rate_down` / `media_rate_1x` | Playback speed |
| `volume_up` / `volume_down` / `mute` | Volume |

Grid editor sets `page_count` (2–4); page 0 is Media on device.

## Auto-reconnect

Remembers `ble_last_id` / `ble_last_name`. On link loss the hub clears the handle without blocking on CoreBluetooth disconnect, re-scans for a fresh peripheral, and retries with backoff. Keep-alive `ping` while linked.

## Data

SQLite at `~/Library/Application Support/TouchDeck Companion/touchdeck.sqlite` maps `action_id` → OpenApp / Volume / Mute / Media / Keyboard.

## Architecture

See [`protocol/gatt.md`](../protocol/gatt.md) and [`docs/companion.md`](../docs/companion.md).
