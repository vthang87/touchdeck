# Provisioning

## Board (ESP32)

1. Power on without saved Wi-Fi → AP `TouchDeck-Setup-XXXX` (password `touchdeck`)
2. Browse `http://192.168.4.1/`
3. Submit SSID, password, device name, BLE name, hostname, OTA password
4. Device stores NVS (`provisioned=true`) and restarts into STA
5. Factory reset: portal `/reset` or clear NVS

BLE HID advertising starts regardless of Wi-Fi so volume control works during setup.

## macOS companion

After the board is on your LAN:

```bash
cd companion
pnpm install
pnpm start
```

1. Scan for `_touchdeck._tcp` or enter `touchdeck.local` port `81`
2. Press **Connect** — confirm log shows `protocol 3`
3. Pair BLE **TouchDeck-XXXX** separately for media keys (optional)

### Accessibility (required for approval alerts)

To push Cursor/Codex approval notifications to the deck:

1. Open **System Settings → Privacy & Security → Accessibility**
2. Enable **TouchDeck Companion**
3. Restart the companion if macOS prompted on first scan

Without this permission, volume sync and tile launch still work; only approval detection is disabled.

See [`docs/approval-notifications.md`](approval-notifications.md) for the full detection and notification logic.
