#!/usr/bin/env bash
# Package PlatformIO usb build artifacts into a web-flash / release bundle.
# Run after: pio run -e usb
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/firmware/.pio/build/usb"
DEST="${1:-$ROOT/web/install/firmware}"
VERSION="${VERSION:-$(grep -E '^#define FIRMWARE_VERSION' "$ROOT/firmware/include/version.h" | sed 's/.*"\(.*\)".*/\1/')}"
FIRMWARE_BASE_URL="${FIRMWARE_BASE_URL:-}"

BOOT_APP0="${PLATFORMIO_HOME:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

for f in "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin"; do
  if [[ ! -f "$f" ]]; then
    echo "error: missing $f — run 'cd firmware && pio run -e usb' first" >&2
    exit 1
  fi
done

if [[ ! -f "$BOOT_APP0" ]]; then
  echo "error: boot_app0.bin not found at $BOOT_APP0" >&2
  exit 1
fi

mkdir -p "$DEST"
cp "$BUILD/bootloader.bin" "$DEST/bootloader.bin"
cp "$BUILD/partitions.bin" "$DEST/partitions.bin"
cp "$BUILD/firmware.bin" "$DEST/firmware.bin"
cp "$BOOT_APP0" "$DEST/boot_app0.bin"

MANIFEST_OUT="${MANIFEST_OUT:-$ROOT/web/install/manifest.json}"
python3 - "$MANIFEST_OUT" "$VERSION" "$FIRMWARE_BASE_URL" <<'PY'
import json
import sys
from pathlib import Path

out, version, base_url = sys.argv[1], sys.argv[2], sys.argv[3]
template = Path(out)
data = json.loads(template.read_text()) if template.exists() else {}
data["name"] = data.get("name", "TouchDeck")
data["version"] = version
data["chipFamily"] = "ESP32-S3"
data["flashMode"] = "qio"
data["flashFreq"] = "80m"
if base_url:
    data["firmwareBaseUrl"] = base_url.rstrip("/") + "/"
elif "firmwareBaseUrl" in data and not data["firmwareBaseUrl"]:
    del data["firmwareBaseUrl"]
data.setdefault(
    "builds",
    [
        {
            "chipFamily": "ESP32-S3",
            "parts": [
                {"path": "bootloader.bin", "offset": 0},
                {"path": "partitions.bin", "offset": 32768},
                {"path": "boot_app0.bin", "offset": 57344},
                {"path": "firmware.bin", "offset": 65536},
            ],
        }
    ],
)
Path(out).write_text(json.dumps(data, indent=2) + "\n")
print(f"Wrote manifest {out} v{version}")
PY

# Release bundle: flat dir with manifest + bins for GitHub Releases
if [[ -n "${RELEASE_DIR:-}" ]]; then
  mkdir -p "$RELEASE_DIR"
  cp "$DEST"/*.bin "$RELEASE_DIR/"
  cp "$MANIFEST_OUT" "$RELEASE_DIR/manifest.json"
  (cd "$RELEASE_DIR" && sha256sum *.bin manifest.json > SHA256SUMS.txt)
  echo "Release bundle: $RELEASE_DIR"
  ls -lh "$RELEASE_DIR"
fi

echo "Packaged firmware v$VERSION → $DEST"
ls -lh "$DEST"/*.bin
