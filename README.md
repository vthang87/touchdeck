# TouchDeck

Firmware for **JC8048W550C** (ESP32-S3, 800×480 RGB + GT911): configurable **LVGL app/media grid**, **BLE HID** media keys, **WebSocket** bridge to a macOS companion, Wi-Fi portal, and OTA.

Repository: https://github.com/vthang87/touchdeck

## Features

- Customizable home grid (2–5 cols × 1–3 rows) with built-in icons/colors
- Media tiles: volume / mute / play-pause / next / previous via **BLE HID**
- App tiles: WebSocket `tile_press` → Electron companion opens bundle ID or `.app` path
- **Approval alerts:** Cursor / Codex approve requests pushed to deck banner + tile highlight (protocol v3)
- Web grid editor at `/grid` (AP `192.168.4.1` or STA `http://touchdeck.local/grid`)
- Wi-Fi setup portal + ArduinoOTA
- Grid profile stored on LittleFS (`/grid.json`)

## Volume / media

| Connection | How |
|---|---|
| USB cable | HID volume **disabled** on this board (GPIO19/20 = GT911 I2C) |
| Bluetooth | Pair **TouchDeck-XXXX**; media tiles send HID Consumer Control |

## Requirements

- PlatformIO
- JC8048W550C board (16 MB flash, OPI PSRAM)
- USB-C data cable for first flash
- Node.js 20+ for the companion (optional)

## Build & flash (USB)

```bash
pio run -e usb -t upload
pio device monitor -b 115200
```

### Flash qua trình duyệt (không cần PlatformIO trên máy)

```bash
./scripts/prepare-web-firmware.sh   # build + copy .bin vào web/install/firmware/
cd web && pnpm serve                # http://127.0.0.1:8787
```

Hoặc TouchDeck Companion → menu **Flash ESP (browser)**.

**GitHub:** tag `v*` → Release tự build; trang giới thiệu + HDSD trên **GitHub Pages**, flash tại `/setup.html`. Cloudflare Worker (tuỳ chọn) sync cùng `web/install/`.

- Giới thiệu / HDSD: https://vthang87.github.io/touchdeck/
- Cài firmware: https://vthang87.github.io/touchdeck/setup.html
- Cloudflare: [`docs/cloudflare-worker.md`](docs/cloudflare-worker.md)
- Web install: [`docs/web-install.md`](docs/web-install.md)

## Grid editor

1. Join AP `TouchDeck-Setup-XXXX` (password `touchdeck`) **or** connect the board to Wi-Fi
2. Open `http://192.168.4.1/grid` or `http://touchdeck.local/grid`
3. Edit cols/rows, labels, actions, bundle IDs → **Save to device** (no reboot)

API: `GET/POST /api/grid`, `POST /api/grid/reset`

## macOS companion

The companion talks to the board over **Wi-Fi (WebSocket, port 81)**, not Bluetooth.

macOS reserves BLE HID peripherals: once the board is paired as a keyboard for media keys, Chromium/CoreBluetooth refuse GATT access to it. Wi-Fi sidesteps that entirely and lets HID keep working.

### Cài nhanh (file đóng gói)

```bash
cd companion
pnpm install
pnpm run dist          # → companion/release/TouchDeck Companion-0.2.0-*.dmg
```

Mở file `.dmg` → kéo **TouchDeck Companion.app** vào Applications → double-click chạy (menu bar).

Lần đầu macOS có thể chặn app chưa ký: **System Settings → Privacy & Security → Open Anyway**, hoặc chuột phải → Open.

CI cũng upload artifact `touchdeck-companion-macos` (DMG + ZIP) trên mỗi push đổi companion.

### Chạy từ source (dev)

```bash
cd companion
pnpm install
pnpm start
```

1. Provision the board's Wi-Fi so it shares your network (see grid editor section)
2. Enter `touchdeck.local` (or the board IP) and port `81`, press **Connect**
3. Tap an app tile — the companion runs `/usr/bin/open` with separated arguments, never a shell
4. Grant **Accessibility** to TouchDeck Companion (System Settings → Privacy & Security) for approval detection

### Approval notifications (Cursor / Codex)

When Cursor IDE or Codex Desktop waits for user approval on your Mac:

1. Companion polls UI every 2.5s via AppleScript (`companion/src/approval_watcher.ts`)
2. Sends `{"op":"notification",...}` over WebSocket
3. Deck shows a pulsing banner, highlights the Cursor/Codex tile, and wakes the display if idle

Full logic, protocol fields, file map, and troubleshooting: **[`docs/approval-notifications.md`](docs/approval-notifications.md)**

Media tiles keep working through BLE HID even if the companion is closed.

## OTA update

```bash
pio run -e ota -t upload --upload-port <hostname>.local
```

Default OTA password: `touchdeck` (change in portal).

## Docs

- [`docs/huong-dan-su-dung.md`](docs/huong-dan-su-dung.md) — **Giới thiệu hệ thống + hướng dẫn sử dụng** (có ảnh)
- [`docs/cloudflare-worker.md`](docs/cloudflare-worker.md) — deploy web installer lên Cloudflare Workers
- [`docs/web-install.md`](docs/web-install.md) — cài firmware ESP qua Chrome/Edge (Web Serial)
- [`docs/approval-notifications.md`](docs/approval-notifications.md) — approval alert flow (Cursor/Codex → deck)
- [`docs/ble-protocol.md`](docs/ble-protocol.md) — BLE GATT & WebSocket protocol v3
- [`docs/provisioning.md`](docs/provisioning.md) — Wi-Fi / device setup
- [`docs/ota-process.md`](docs/ota-process.md) — OTA update flow
- [`docs/jc8048w550c-platformio-wifi-ble-ota.md`](docs/jc8048w550c-platformio-wifi-ble-ota.md) — board architecture

## License

[MIT](LICENSE) © 2026 Thang Dang
