# OTA process

1. Flash once over USB: `cd firmware && pio run -e usb -t upload`
2. Provision Wi-Fi via AP portal
3. Confirm Serial shows `[OTA] Ready on <hostname>.local`
4. Update: `cd firmware && pio run -e ota -t upload --upload-port <hostname>.local`
5. Auth: OTA password from NVS (default `touchdeck`)

Optional: Companion can write GATT `{"op":"enter_ota"}` as a hint; the actual binary still uploads over Wi-Fi / `pio`, not BLE.

On failure, recover with USB env. Dual OTA partitions keep the previous slot for rollback by bootloader when the new image fails to boot.
