# TouchDeck

Firmware for **JC8048W550C** (ESP32-S3, 800×480 RGB + GT911): configurable **LVGL app/media grid**, **BLE GATT** events to a **Rust + Tauri** macOS companion, Wi-Fi portal/OTA/icons.

Repository: https://github.com/vthang87/touchdeck

Protocol **v4** — the board is a thin device (`action_id` over GATT). The companion owns OpenApp / media / volume. No BLE HID on the board.

## Features

- **Workspace pager:** page 0 = Media (Now Playing), pages 1…N−1 = shortcut grids (2–4 pages)
- Customizable shortcut grid (2–5 cols × 1–3 rows) with built-in icons/colors
- Media & app tiles: GATT `tile_press` → Companion Action Engine
- Now Playing sync (any macOS player via MediaRemote / JXA), seek ±10s, playback rate, volume
- Vietnamese UI fonts on Media / notifications; clock shows track title while playing
- Volume UI on the deck synced from the Mac over GATT Command
- Web grid editor at `/grid` (AP `192.168.4.1` or STA `http://touchdeck.local/grid`)
- Wi-Fi setup portal + ArduinoOTA + custom icons on SD
- Grid profile on LittleFS with multi-page `action_id` tiles

## Requirements

- PlatformIO
- JC8048W550C board (16 MB flash, OPI PSRAM)
- USB-C data cable for first flash
- Node.js 20+ and Rust (for the Tauri companion)

## Build & flash (USB)

```bash
cd firmware
pio run -e usb -t upload
pio device monitor -b 115200
```

### Browser flash (Web Serial)

```bash
./scripts/prepare-web-firmware.sh   # build + copy .bin into web/install/firmware/
cd web && pnpm serve                # http://127.0.0.1:8787
```

**GitHub:** tag `v*` → Release; intro + guide on **GitHub Pages**, flash at `/setup.html`.

- Intro / guide (Pages): https://vthang87.github.io/touchdeck/
- Full docs: [English](docs/user-guide.md) · [Vietnamese](docs/huong-dan-su-dung.md)
- Firmware install: https://vthang87.github.io/touchdeck/setup.html
- Protocol: [`protocol/gatt.md`](protocol/gatt.md)
- Web install: [`docs/web-install.md`](docs/web-install.md)

## Grid editor

1. Join AP `TouchDeck-Setup-XXXX` (password `touchdeck`) **or** connect the board to Wi-Fi
2. Open `http://192.168.4.1/grid` or `http://touchdeck.local/grid`
3. Set `action_id` (and optional legacy target) → **Save to device**

## macOS companion (Tauri)

Primary link is **Bluetooth GATT** (not WebSocket).

### Quick install

- **Releases:** https://github.com/vthang87/touchdeck/releases/latest  
  Artifact: `TouchDeck-Companion-0.3.1-mac-arm64.dmg` (version follows `companion/package.json`)

Open the `.dmg` → drag the app into Applications. First launch: **Privacy & Security → Open Anyway**.

Grant:

- **Bluetooth**
- **Accessibility** (media / keyboard / volume simulation)

### Dev / package

```bash
cd companion
pnpm install
pnpm tauri dev          # development
pnpm run dist           # .app + .dmg
```

1. Power the board with BLE pairing mode on
2. Companion → **Scan BLE** → Connect to `TouchDeck-XXXX` (auto-reconnect remembers the last device)
3. Swipe Media ↔ shortcuts; tap a tile — companion maps `action_id` via SQLite
4. Now Playing + volume are pushed back to the deck over GATT

The Electron companion (legacy WebSocket) lives in [`archive/companion-electron/`](archive/companion-electron/) and is not shipped.

## OTA update

```bash
cd firmware
pio run -e ota -t upload --upload-port <hostname>.local
```

Default OTA password: `touchdeck`.

## Docs

- [`docs/user-guide.md`](docs/user-guide.md) — system overview & user guide (**English**, primary)
- [`docs/huong-dan-su-dung.md`](docs/huong-dan-su-dung.md) — Vietnamese user guide
- [`docs/companion.md`](docs/companion.md) — companion Tauri overview
- [`protocol/gatt.md`](protocol/gatt.md) — GATT protocol v4
- [`docs/ble-protocol.md`](docs/ble-protocol.md) — legacy v3 notes (superseded)
- [`docs/web-install.md`](docs/web-install.md) — Web Serial flash
- [`docs/ota-process.md`](docs/ota-process.md) — OTA
- [`docs/jc8048w550c-platformio-wifi-ble-ota.md`](docs/jc8048w550c-platformio-wifi-ble-ota.md) — board notes
- [`archive/companion-electron/`](archive/companion-electron/) — legacy Electron companion

## License

MIT — see [LICENSE](LICENSE).
