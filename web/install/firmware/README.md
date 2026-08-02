# TouchDeck web installer binaries

Place build artifacts here before serving the installer page.

## Quick prepare (from repo root)

```bash
./scripts/prepare-web-firmware.sh
```

This runs `cd firmware && pio run -e usb` and copies:

| File | Flash offset |
|---|---|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xE000` |
| `firmware.bin` | `0x10000` |

Offsets match `partitions_16MB.csv` and `web/install/manifest.json`.

## Serve locally

```bash
cd web
pnpm install
pnpm serve
```

Open http://127.0.0.1:8787 in **Chrome** or **Edge** (Web Serial required).
