# Thông báo approve request lên TouchDeck

Tài liệu mô tả toàn bộ logic đẩy thông báo **approval request** từ **Cursor** và **Codex** trên macOS lên màn hình TouchDeck.

## Tổng quan

Khi Cursor IDE hoặc Codex Desktop trên Mac đang chờ người dùng phê duyệt (chạy lệnh, sửa file, v.v.), companion app sẽ:

1. Phát hiện trạng thái chờ approve qua AppleScript + System Events.
2. Gửi JSON qua WebSocket tới ESP32.
3. Deck hiển thị banner cảnh báo, highlight tile tương ứng, và tự bật sáng màn hình nếu đang idle.

Khi người dùng approve hoặc từ chối xong, companion gửi lệnh xóa thông báo — banner và highlight biến mất.

## Kiến trúc

```text
┌─────────────────────────────────────────────────────────────────┐
│ macOS                                                           │
│                                                                 │
│  ┌──────────┐    AppleScript      ┌──────────────────────────┐  │
│  │ Cursor   │◄──(Accessibility)──│ approval_watcher.ts      │  │
│  │ Codex    │                     │  poll mỗi 2.5s           │  │
│  └──────────┘                     └────────────┬─────────────┘  │
│                                                 │ IPC             │
│                                    ┌────────────▼─────────────┐  │
│                                    │ main.ts                  │  │
│                                    │  approval-update event   │  │
│                                    └────────────┬─────────────┘  │
│                                                 │                 │
│                                    ┌────────────▼─────────────┐  │
│                                    │ renderer/app.js          │  │
│                                    │  WebSocket client :81    │  │
│                                    └────────────┬─────────────┘  │
└─────────────────────────────────────────────────┼─────────────────┘
                                                  │ Wi-Fi
                                                  │ ws://touchdeck.local:81/
┌─────────────────────────────────────────────────▼─────────────────┐
│ ESP32-S3 (TouchDeck)                                            │
│                                                                 │
│  event_server.cpp ──► notification_overlay.cpp                  │
│       │                      │                                  │
│       │                      ├── banner (lv_layer_top)          │
│       │                      └── idleManagerWakeForAlert()      │
│       │                                                         │
│       └──► home_grid_screen.cpp                                 │
│              └── tile highlight (cursor / codex)                │
└─────────────────────────────────────────────────────────────────┘
```

### Kênh giao tiếp

| Hướng | Kênh | Ghi chú |
|---|---|---|
| Mac → Deck | WebSocket port **81** | Kênh chính cho notification |
| Deck → Mac | WebSocket `tile_press` | Không dùng cho approve (chưa implement tap-to-approve) |
| BLE GATT | Không dùng | macOS chặn GATT khi board đã pair HID |

---

## Luồng xử lý end-to-end

### 1. Phát hiện approval trên Mac

**File:** `companion/src/approval_watcher.ts`

Companion chạy `scanApprovalRequests()` mỗi **2.5 giây** (`APPROVAL_POLL_MS` trong `main.ts`).

Script AppleScript quét process **Cursor** và **Codex** qua System Events:

- Duyệt tất cả `windows` của process.
- Gom text từ `static texts` và `buttons`.
- Coi là **đang chờ approve** nếu gặp một trong các pattern:
  - `"Waiting for approval"` hoặc `"waiting for approval"`
  - Đồng thời có `"Approve"` **và** `"Run"` (UI card approve của Cursor)

Kết quả trả về dạng:

```text
cursor|Cursor needs approval
codex|Codex approve request
```

Parser chuyển thành object:

```typescript
{
  id: "cursor" | "codex",
  source: "cursor" | "codex",
  title: "Cursor" | "Codex",
  body: "Cursor needs approval"
}
```

**Dedup:** `approvalKey()` nối `source:body` để tránh gửi lại khi không đổi.

**Yêu cầu:** TouchDeck Companion phải có quyền **Accessibility** trong System Settings → Privacy & Security → Accessibility. Không có quyền này, `osascript` không đọc được UI → scan trả lỗi.

### 2. IPC main process → renderer

**File:** `companion/src/main.ts`

- `startApprovalWatch()` chạy khi app khởi động.
- Khi WebSocket connected (`set-connection-status` ≠ Disconnected), watcher tiếp tục poll.
- Khi disconnected, gửi `{ pending: [] }` để xóa state.
- Event IPC: `approval-update` với payload `{ pending: ApprovalPending[] }`.

**File:** `companion/src/preload.ts`

Expose `window.touchdeck.onApprovalUpdate(cb)` cho renderer.

### 3. Renderer gửi lên deck

**File:** `companion/src/renderer/app.js`

`syncApprovalsToDeck(pending)` so sánh `activeApprovals` (Map) với danh sách mới:

| Thay đổi | Hành động WebSocket |
|---|---|
| Source mới xuất hiện | `{"op":"notification", ...}` |
| Body thay đổi | Gửi lại `notification` |
| Source biến mất | `{"op":"notification_clear","id":"..."}` |
| Tất cả clear | `{"op":"notification_clear_all":true}` |

Khi WebSocket reconnect, gửi lại toàn bộ `activeApprovals` hiện tại.

### 4. Firmware nhận WebSocket

**File:** `src/net/event_server.cpp`

Handler `WStype_TEXT`, JSON tối đa **512 byte**, `StaticJsonDocument<512>`.

| `op` | Hành động |
|---|---|
| `ping` | Trả `{"type":"pong"}` |
| `volume` | `homeGridScreenSetVolume(level, muted)` |
| `notification` | `notificationOverlaySet()` + `homeGridScreenSetApprovalHighlight(source, true)` |
| `notification_clear` | `notificationOverlayClear(id)` + `homeGridScreenSetApprovalHighlight(id, false)` |
| `notification_clear_all` | `notificationOverlayClearAll()` + `homeGridScreenClearApprovalHighlights()` |

### 5. Hiển thị trên deck

#### Banner overlay

**File:** `src/ui/screens/notification_overlay.cpp`

- Tạo trên `lv_layer_top()` — hiển thị trên mọi màn hình (grid, clock, dim).
- Kích thước 760×72 px, căn top-center.
- Tối đa **2** notification slot (`cursor`, `codex`).
- Icon 🔔 (`LV_SYMBOL_BELL`), title, body.
- Viền màu theo source:
  - Cursor: `#6366F1`
  - Codex: `#0D8A6A`
- `notificationOverlayTick()` nhấp nháy viền mỗi 600 ms.

#### Wake màn hình

**File:** `src/system/idle_manager.cpp` — `idleManagerWakeForAlert()`

- Reset `s_last_activity_ms`.
- Nếu không ở trạng thái Active → set `s_pending_active` (chuyển về home grid ở tick kế).
- Bật backlight full (`APP_BRIGHTNESS_FULL`).

#### Highlight tile

**File:** `src/ui/screens/home_grid_screen.cpp`

- `s_cursor_approval` / `s_codex_approval` flags.
- Tile có `icon == "cursor"` hoặc `"codex"` nhận viền 3 px màu `#FBBF24`.
- Cập nhật trong `updateTileAvailability()` mỗi khi state đổi.

#### UI lifecycle

**File:** `src/ui/ui_manager.cpp`

- `notificationOverlayBegin()` gọi sau `clockScreenCreate()`.
- `notificationOverlayTick()` gọi mỗi vòng `uiManagerTick()`.

---

## Giao thức WebSocket (protocol v3)

`PROTOCOL_VERSION = 3` (`include/version.h`, `platformio.ini`).

### Deck → companion (không đổi)

```json
{"type":"hello","model":"JC8048W550C","fw":"0.2.0","protocol":3}
{"type":"tile_press","id":"cursor","t":12345,"target":{"kind":"bundle","value":"com.todesktop.230313mzl4w4u92"}}
{"type":"media_press","action":"volume_up","handled":true,"t":12345}
{"type":"pong"}
```

### Companion → deck

**Volume sync** (đã có từ v2):

```json
{"op":"volume","level":65,"muted":false}
```

**Notification** (mới v3):

```json
{
  "op": "notification",
  "id": "cursor",
  "source": "cursor",
  "title": "Cursor",
  "body": "Cursor needs approval"
}
```

| Field | Bắt buộc | Mô tả |
|---|---|---|
| `op` | ✓ | Luôn `"notification"` |
| `id` | ✓ | ID duy nhất, thường trùng `source` |
| `source` | | `"cursor"` hoặc `"codex"` — dùng highlight tile |
| `title` | | Tiêu đề banner (mặc định `"Approval"`) |
| `body` | | Nội dung (mặc định `"Waiting for approval"`) |

**Xóa một notification:**

```json
{"op":"notification_clear","id":"cursor"}
```

`id` có thể là notification id hoặc `source` — firmware tìm theo cả hai.

**Xóa tất cả:**

```json
{"op":"notification_clear_all":true}
```

**Keepalive:**

```json
{"op":"ping"}
```

---

## File tham chiếu

### Companion (macOS)

| File | Vai trò |
|---|---|
| `companion/src/approval_watcher.ts` | AppleScript scan, parse, dedup key |
| `companion/src/main.ts` | Poll timer, IPC `approval-update`, lifecycle |
| `companion/src/preload.ts` | `onApprovalUpdate` bridge |
| `companion/src/renderer/app.js` | `syncApprovalsToDeck()`, WebSocket send |
| `companion/src/renderer/electron-api.d.ts` | Type definitions |

### Firmware (ESP32)

| File | Vai trò |
|---|---|
| `src/net/event_server.cpp` | Parse WebSocket inbound, route notification ops |
| `src/ui/screens/notification_overlay.h/.cpp` | Banner UI, slot storage, pulse animation |
| `src/ui/screens/home_grid_screen.cpp` | Tile highlight cursor/codex |
| `src/system/idle_manager.h/.cpp` | `idleManagerWakeForAlert()` |
| `src/ui/ui_manager.cpp` | Init + tick notification overlay |
| `include/version.h` | `PROTOCOL_VERSION 3` |

---

## Cài đặt và vận hành

### 1. Flash firmware protocol v3

```bash
pio run -e usb -t upload
```

Hoặc OTA:

```bash
pio run -e ota -t upload --upload-port touchdeck.local
```

### 2. Chạy companion

```bash
cd companion
pnpm install
pnpm start
```

### 3. Kết nối deck

1. Board và Mac cùng Wi-Fi.
2. Companion scan Bonjour `_touchdeck._tcp` hoặc nhập `touchdeck.local:81`.
3. Nhấn **Connect** — log hiện `protocol 3`.

### 4. Cấp quyền Accessibility

System Settings → Privacy & Security → Accessibility → bật **TouchDeck Companion**.

Lần đầu macOS có thể hỏi khi companion chạy AppleScript. Nếu từ chối, watcher log:

```text
[approval] scan failed: ...
```

### 5. Kiểm tra

1. Mở Cursor, yêu cầu agent chạy lệnh cần approve (ví dụ `curl`).
2. Khi UI hiện "Waiting for approval", trong vòng ~2.5s deck sẽ:
   - Bật sáng (nếu đang clock/off).
   - Hiện banner 🔔 "Cursor".
   - Viền vàng tile Cursor.
3. Approve hoặc reject trên Mac → banner biến mất.

Serial monitor deck:

```text
[WS] notification cursor (cursor)
[NOTIFY] set id=cursor source=cursor title=Cursor
[IDLE] wake for alert
[WS] notification_clear cursor
[NOTIFY] clear id=cursor
```

---

## Giới hạn hiện tại

| Hạng mục | Trạng thái |
|---|---|
| Cursor IDE (local) | ✓ Hỗ trợ |
| Codex Desktop app | ✓ Hỗ trợ |
| Cursor Cloud Agent (web/mobile) | ✗ Không detect — approve ở cursor.com |
| Codex CLI / TUI | ✗ Chưa hỗ trợ — cần watcher riêng |
| Tap tile trên deck để approve | ✗ Chưa implement |
| BLE notification mirror | ✗ Chỉ WebSocket |

### Độ trễ

Polling **2.5 giây** — không real-time. AppleScript scan có thể mất vài giây nếu Cursor mở nhiều window.

### False positive / false negative

- **False positive:** UI có text "Approve" + "Run" nhưng không phải agent approval.
- **False negative:** Cursor đổi copy UI (không còn "Waiting for approval") → cần cập nhật pattern trong `approval_watcher.ts`.
- **High Contrast theme** trên Cursor có thể ẩn nút approve trên Mac — không ảnh hưởng detection text nhưng user khó approve trên Mac.

---

## Mở rộng trong tương lai

1. **Deck → Mac `notification_action`:** tap tile Cursor/Codex khi đang highlight → `open -b` focus app.
2. **Codex CLI watcher:** đọc TUI state hoặc log file.
3. **Cấu hình trong portal:** bật/tắt notification, đổi poll interval.
4. **BLE mirror:** gửi notification lên GATT Event characteristic cho client không phải macOS.

---

## Troubleshooting

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Deck không hiện gì | Companion chưa Connect | Kiểm tra WebSocket, log `Connected to touchdeck.local` |
| Deck không hiện gì | Firmware cũ (protocol 2) | Flash lại firmware v3 |
| Companion log lỗi scan | Thiếu Accessibility | Cấp quyền, restart companion |
| Banner không tắt | Approval UI vẫn còn text match | Approve/reject trên Mac; kiểm tra pattern |
| Scan chậm | Nhiều Cursor windows | Đóng bớt window; giảm số project mở |
| Chỉ volume sync, không notification | `app.js` cũ | `pnpm run build` lại companion |
