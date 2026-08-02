# BLE / WebSocket protocol — legacy notes

**Current protocol is v4:** see [`../protocol/gatt.md`](../protocol/gatt.md).

Protocol v3 (BLE HID + WebSocket companion on port 81) is **superseded**. Summary of the change:

| | v3 | v4 |
|---|---|---|
| Media / volume keys | BLE HID on board | Companion (osascript / Accessibility) |
| App launch | WebSocket `tile_press` | GATT `tile_press` with `action_id` |
| Volume UI sync | WS `op:volume` | GATT Command `op:volume` |
| Companion | Electron | Tauri + Rust (`btleplug`) |

Approval notifications (Cursor/Codex) were WS-only in v3 and are **not** in the v4 MVP; they can be re-ported over GATT later.

Historical v3 field docs remain below for reference when reading old firmware / [`archive/companion-electron/`](../archive/companion-electron/) code.

---

## Legacy v3 GATT (still same UUID family)

Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`

| Characteristic | UUID | Properties |
|---|---|---|
| Command | `...0002...` | Write |
| Event | `...0003...` | Notify |
| Status | `...0004...` | Read / Notify |
| Device Info | `...0005...` | Read |

v3 `tile_press` used `id` + `target`; v4 uses `action_id` (see protocol doc).

## Legacy v3 WebSocket (port 81)

Removed from the firmware boot path in v4. The `event_server` sources remain in-tree but are not started.
