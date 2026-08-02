# TouchDeck — Giới thiệu hệ thống & Hướng dẫn sử dụng

TouchDeck là bàn phím cảm ứng 7" (board **JC8048W550C**, ESP32-S3) dùng để mở app trên macOS và chỉnh volume/media qua Companion Tauri (BLE GATT) — cấu hình qua Companion hoặc web portal.

| | |
|---|---|
| Repo | https://github.com/vthang87/touchdeck |
| Trang giới thiệu + HDSD | https://vthang87.github.io/touchdeck/ |
| Cài firmware (USB) | https://vthang87.github.io/touchdeck/setup.html |
| Portal (đã nối Wi‑Fi) | http://touchdeck.local |
| Grid editor | http://touchdeck.local/grid |

---

## 1. Hệ thống gồm những gì?

```mermaid
flowchart LR
  touch[TouchDeck LCD + GT911] -->|GATT tile_press action_id| companion[TouchDeck Companion Tauri]
  companion -->|open / volume / media / seek| mac[macOS]
  companion -->|GATT now_playing + volume| touch
  browser[Chrome] -->|HTTP portal OTA icons| touch
```

| Thành phần | Vai trò |
|---|---|
| **Firmware trên board** | UI LVGL (Media + shortcuts), BLE GATT peripheral, Wi‑Fi portal / OTA / icon |
| **TouchDeck Companion** (Tauri, macOS) | Scan/connect/auto-reconnect BLE, Now Playing → board, map `action_id` → app / volume / media |
| **Config portal** | Wi‑Fi, Bluetooth pairing, idle, tên thiết bị |
| **Grid editor** | Số trang (2–4) + lưới shortcut (`action_id`); trang Media không chỉnh bằng tile editor |
| **Web installer** | Flash firmware qua USB (Chrome/Edge) |

Kênh chính: **Bluetooth GATT**. Wi‑Fi chỉ còn portal / OTA / icon — không còn WebSocket launch.
---

## 2. Màn hình trên desk

Thiết bị dùng **2–4 trang** (mặc định **2**):

| Trang | Nội dung |
|---|---|
| **0 — Media** | Now Playing (title/artist tiếng Việt, progress, seek ±10s, tốc độ −/1x/+, transport, volume). Companion đẩy qua GATT. |
| **1…N-1** | Lưới shortcut (mở app / URL / macro). Vuốt ngang hoặc chạm **dots** để đổi trang. |

Vuốt ngang từ nút/tile **không** kích hoạt nút (firmware theo dõi khoảng cách kéo). Trang Media xám / gợi ý kết nối Companion khi chưa nối GATT. Trang đang xem được nhớ qua NVS.

![Màn hình home grid TouchDeck](images/home-grid.png)

> Ảnh chụp trực tiếp từ LVGL trên board (`GET /api/screenshot`).

**Chụp lại màn hình:**

```bash
./scripts/capture-screenshot.sh              # → docs/images/home-grid.png
./scripts/capture-screenshot.sh 192.168.0.183
```

**Ý nghĩa thanh trạng thái (góc phải, trang shortcut):**

| Icon | Ý nghĩa |
|---|---|
| Volume % | Mức loa Mac (Companion đẩy qua GATT) |
| Bluetooth | Xanh = GATT central đã nối; `pair` / `idle` / `off` |
| ⇅ | Xanh = companion GATT linked; xám = chưa nối → tile host bị mờ |
| Wi‑Fi | Xanh = STA; vàng = đang AP setup |

---

## 3. Cài đặt lần đầu

### 3.1 Flash firmware

Cách nhanh nhất — Chrome/Edge + USB‑C **có data**:

1. Mở [trình cài firmware](https://vthang87.github.io/touchdeck/setup.html)
2. **Kết nối USB** → chọn cổng ESP32‑S3
3. (Khuyên dùng lần đầu) bật xóa flash → **Ghi firmware**

![Web installer](images/web-installer.png)

Giới thiệu & HDSD trên Pages: https://vthang87.github.io/touchdeck/

Hoặc local:

```bash
./scripts/prepare-web-firmware.sh
cd web && pnpm serve   # http://127.0.0.1:8787
```

Chi tiết: [`web-install.md`](web-install.md).

### 3.2 Kết nối Wi‑Fi

1. Board chưa có Wi‑Fi → phát AP `TouchDeck-Setup-XXXX`, mật khẩu `touchdeck`
2. Mở http://192.168.4.1 → nhập SSID / mật khẩu / tên thiết bị / hostname / OTA password
3. **Save & Restart** → sau đó vào bằng http://touchdeck.local

![Portal cấu hình Wi‑Fi / thiết bị](images/device-settings.png)

### 3.3 Bluetooth (GATT)

1. Trên board: bật **Bluetooth enabled** (+ **Pairing mode** lần đầu) trong portal / Companion → **Device**
2. Trên Mac: cấp quyền **Bluetooth** cho TouchDeck Companion (System Settings → Privacy & Security → Bluetooth)
3. Companion → **Scan BLE** → **Connect** tới `TouchDeck-XXXX`

Không ghép như bàn phím HID — Companion là GATT central. Volume/media do app xử lý (không có HUD hệ thống).

### 3.4 Companion trên Mac (Tauri)

**Tải nhanh (Apple Silicon):**  
[TouchDeck-Companion-0.3.0-mac-arm64.dmg](https://github.com/vthang87/touchdeck/releases/latest/download/TouchDeck-Companion-0.3.0-mac-arm64.dmg) · [Tất cả releases](https://github.com/vthang87/touchdeck/releases/latest)

1. Mở `.dmg` → kéo **TouchDeck Companion.app** vào Applications  
2. Chạy app. Lần đầu nếu macOS chặn: chuột phải → **Open** / Privacy & Security → Open Anyway  
3. Tab **Connect** → **Scan BLE** → Connect  
4. Cấp **Accessibility** (media / keyboard / volume simulation)

Tabs hữu ích:

| Tab | Việc làm |
|---|---|
| **Connect** | Scan/Connect BLE, virtual keyboard, auto-reconnect |
| **Grid** / **Device** | Cấu hình board qua Wi‑Fi (`touchdeck.local`) |
| **Profile** | Map `action_id` → open_app / media / volume / keyboard |
| **Log** | Sự kiện GATT / lỗi |

Tự build:

```bash
cd companion && pnpm install && pnpm run dist
```

Source Electron cũ (WebSocket): [`../archive/companion-electron/`](../archive/companion-electron/).

![TouchDeck Companion](images/companion-ui.png)

Khi GATT đã nối, icon ⇅ trên màn hình xanh và các nút mở app sáng lại.

---

## 4. Hướng dẫn sử dụng hàng ngày

### 4.1 Mở ứng dụng

Chạm tile **App** — Companion nhận GATT `tile_press` với `action_id` và chạy `/usr/bin/open` theo map SQLite.

Nếu tile **mờ**: Companion chưa nối BLE — Scan + Connect lại.

### 4.2 Volume & media

Tất cả media/volume do Companion xử lý (không còn BLE HID trên board).

- **Now Playing:** lấy từ session hệ thống (Safari/YouTube, Music, Spotify, …) qua MediaRemote/JXA — không chỉ Music/Spotify.
- **Seek ±10s:** đặt vị trí phát; không nhảy next/prev khi seek lỗi.
- **Tốc độ:** − / 1x / + (0.75×…2×) khi app hỗ trợ; Safari có thể cần bật *Allow JavaScript from Apple Events*.
- Volume ± khoảng 3%, **không có HUD hệ thống**. Mute / play / next / prev / seek cần **Accessibility**.

### 4.3 Đổi trang deck

Vuốt ngang giữa Media ↔ shortcuts, hoặc chạm dots. Vuốt bắt đầu trên nút vẫn đổi trang và **không** bấm nút. Companion / portal **Grid** đặt **Total pages** 2–4.

### 4.4 Màn hình nghỉ (idle)

Mặc định (đổi được trong portal / Companion → Device):

| Sau | Hành động |
|---|---|
| 30 s | Dim xuống ~30% |
| 120 s | Màn hình đồng hồ |
| 300 s | Dim 2 (~30%) |
| 1800 s (30 phút) | Tắt backlight |

Chạm để đánh thức. **Lần chạm wake từ đồng hồ/tắt màn không kích hoạt nút** — nhấc tay rồi chạm lần nữa mới bấm tile.

Khi đang phát media, đồng hồ hiện thêm dòng **tên bài — nghệ sĩ** dưới ngày.

Cỡ chữ đồng hồ: **Clock font size** (48 / 72 / 96 / 128 / 160 px).

### 4.5 Cảnh báo duyệt Cursor / Codex

Chưa nằm trong MVP v4 (GATT). Bản Electron cũ (`archive/companion-electron/`) dùng WebSocket — xem [`approval-notifications.md`](approval-notifications.md) (legacy).

---

## 5. Tuỳ chỉnh lưới nút

Mở http://touchdeck.local/grid (hoặc Companion → **Grid**):

1. Chọn **Total pages** (2–4; trang 0 luôn là Media)
2. Chọn **Edit shortcut page** cần sửa
3. Chọn **Columns** / **Rows** (2–5 × 1–3)
4. Mỗi ô: `label`, `icon`, `color`, `action`, **`action_id`**
5. Action `app` có thể giữ legacy `target` (bundle/path) để gợi ý; Companion map theo `action_id`
6. Upload PNG icon lên thẻ SD nếu cần
7. **Save** — áp dụng ngay, không reboot

Companion tab **Profile** chỉnh map `action_id` → bundle / media / keyboard. Tab **Grid** / **Device** chỉnh board qua HTTP (cùng API portal).

---

## 6. Cập nhật firmware

| Cách | Lệnh / URL |
|---|---|
| USB Web Installer | https://vthang87.github.io/touchdeck/setup.html |
| PlatformIO USB | `cd firmware && pio run -e usb -t upload` |
| OTA | `cd firmware && pio run -e ota -t upload --upload-port touchdeck.local` |

Mật khẩu OTA mặc định: `touchdeck`. Chi tiết: [`ota-process.md`](ota-process.md).

---

## 7. Xử lý sự cố nhanh

| Hiện tượng | Việc kiểm tra |
|---|---|
| Nút mờ / không mở app | Companion đã Connect BLE? Icon BT xanh? |
| BLE rớt / treo “Linked” | Restart Companion — bản mới auto-reconnect (re-scan, không block disconnect). Xem Log: `link cleared` → `Attempting reconnect` |
| Now Playing trống | Có media trên Control Center? Companion Log có dòng `Now Playing:`? |
| Mute / media không chạy | Accessibility cho Companion? |
| Rate không đổi (Safari) | Safari → Develop / settings: Allow JavaScript from Apple Events |
| Volume không có HUD | Đúng hành vi v4 (không BLE HID) |
| Vuốt bị bấm nút | Flash firmware mới (drag gate) |
| Dim thành tắt hẳn | PWM backlight 1 kHz; **Dim brightness %** ≥ ~20 |
| Wake bị bấm nhầm tile | Đã chặn; nhấc tay rồi chạm lại |
| Portal không mở | Thử IP hoặc AP setup |
| Flash Web Serial lỗi | Chrome/Edge, cáp data, cổng ESP32‑S3 |

---

## 8. Tài liệu kỹ thuật liên quan

- [`../companion/README.md`](../companion/README.md) — companion Tauri
- [`../archive/companion-electron/`](../archive/companion-electron/) — companion Electron (legacy)
- [`../protocol/gatt.md`](../protocol/gatt.md) — GATT protocol v4
- [`provisioning.md`](provisioning.md) — Wi‑Fi / BLE setup
- [`web-install.md`](web-install.md) — flash qua trình duyệt
- [`ble-protocol.md`](ble-protocol.md) — legacy v3 notes
- [`approval-notifications.md`](approval-notifications.md) — approve (legacy / phase sau)
- [`ota-process.md`](ota-process.md) — OTA
- [`cloudflare-worker.md`](cloudflare-worker.md) — deploy installer (tuỳ chọn)

---

## Ảnh trong tài liệu

| File | Nội dung |
|---|---|
| `images/home-grid.png` | Màn hình desk — LVGL snapshot qua `/api/screenshot` |
| `images/grid-editor.png` | Grid editor trên board |
| `images/device-settings.png` | Portal Wi‑Fi / idle / Bluetooth |
| `images/companion-ui.png` | TouchDeck Companion |
| `images/web-installer.png` | GitHub Pages installer |
| `images/home-grid-mock.html` | Mock HTML (tham khảo, không còn dùng làm ảnh chính) |

---

## License

[MIT](../LICENSE) © 2026 Thang Dang
