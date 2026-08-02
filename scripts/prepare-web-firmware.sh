#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "Building TouchDeck firmware (usb env)…"
(cd "$ROOT" && pio run -e usb)
exec "$ROOT/scripts/package-firmware-bundle.sh" "$ROOT/web/install/firmware"
