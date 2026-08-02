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
- **Accessibility** — media / keyboard simulation

## Data

SQLite at `~/Library/Application Support/TouchDeck Companion/touchdeck.sqlite` maps `action_id` → OpenApp / Volume / Mute / Media / Keyboard.

## Architecture

See [`protocol/gatt.md`](../protocol/gatt.md).
