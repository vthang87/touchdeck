# Companion (Tauri) — overview

Primary macOS host for TouchDeck protocol **v4**.

| | |
|---|---|
| Path | [`../companion/`](../companion/) |
| Stack | Rust + Tauri 2 + React + `btleplug` |
| Protocol | [`../protocol/gatt.md`](../protocol/gatt.md) |
| Legacy | [`../archive/companion-electron/`](../archive/companion-electron/) |

## Features (0.3.x)

- BLE scan / connect / **auto-reconnect** (persisted last device)
- SQLite **Profile** map: `action_id` → open_app / open_url / media / volume / mute / keyboard
- **Virtual keyboard** (enigo): media, volume, ⌥⇧Vol fine steps, shortcuts, type text
- **Grid** + **Device** tabs — HTTP to board portal (`touchdeck.local`)
- Tray + hide-on-close; Accessibility + Bluetooth permission UX

## Dev / package

```bash
cd companion
pnpm install
pnpm tauri dev
pnpm run dist    # → src-tauri/target/release/bundle/dmg/*.dmg
```

CI: `.github/workflows/companion.yml` builds frontend, `cargo check`, and packages macOS DMG on `macos-latest`.
