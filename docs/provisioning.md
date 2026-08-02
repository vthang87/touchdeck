# Provisioning

## Wi‑Fi

1. Boot board → AP `TouchDeck-Setup-XXXX` (password `touchdeck`) if not provisioned
2. Open `http://192.168.4.1` → enter STA SSID/password → Save (reboots)
3. Portal then available at `http://touchdeck.local` (or device IP)

Wi‑Fi is used for **portal, OTA, and icon upload** only (protocol v4).

## Bluetooth (GATT)

1. Enable Bluetooth + pairing mode in the portal (defaults on)
2. Board advertises as `TouchDeck-XXXX`
3. Open **TouchDeck Companion** → Scan BLE → Connect

Tiles that need the host stay disabled until a GATT central is connected.

There is **no BLE HID** keyboard profile in v4 — CoreBluetooth can use the custom GATT service normally on macOS.
