# Companion (Tauri) — overview

Primary macOS host for TouchDeck protocol **v4**.

| | |
|---|---|
| Path | [`../companion/`](../companion/) |
| Stack | Rust + Tauri 2 + React + `btleplug` |
| Protocol | [`../protocol/gatt.md`](../protocol/gatt.md) |
| Legacy | [`../archive/companion-electron/`](../archive/companion-electron/) |

## Features (0.3.x)

- BLE scan / connect / **auto-reconnect** (persisted last device + name)
- Resilient link recovery on macOS: fresh scan after disconnect, non-blocking link clear, keep-alive `ping`, bounded CB timeouts
- SQLite **Profile** map: `action_id` → open_app / open_url / media / volume / mute / keyboard / seek / rate
- **Now Playing** poller (`MRNowPlayingRequest` via JXA — Safari/YouTube and system players; Music/Spotify AppleScript fallback) → GATT `now_playing`
- Seek ±10s via MediaRemote elapsed time; playback rate (− / 1x / +) where the host app allows it
- **Virtual keyboard** (enigo): media, volume, ⌥⇧Vol fine steps, shortcuts, type text
- **Grid** + **Device** tabs — HTTP to board portal (`touchdeck.local`); page count 2–4
- Tray + hide-on-close; Accessibility + Bluetooth permission UX

## Dev / package

```bash
cd companion
pnpm install
pnpm tauri dev
pnpm run dist    # → src-tauri/target/release/bundle/dmg/*.dmg
```

CI: `.github/workflows/companion.yml` builds frontend, `cargo check`, and packages macOS DMG on `macos-latest`.

## macOS notes

| Topic | Detail |
|---|---|
| Now Playing | Primary path is JXA `MRNowPlayingRequest` (works on macOS 15.4+ where MediaRemote FFI is blocked for 3rd-party apps). |
| Seek | Sets elapsed time; does **not** fall back to next/prev. |
| Rate | Best-effort (browser JS / YouTube shortcuts). Safari may need **Allow JavaScript from Apple Events**. |
| Reconnect | After `DeviceDisconnected`, companion drops the stale `Peripheral` and re-scans — never awaits a hung CoreBluetooth `disconnect()`. |
