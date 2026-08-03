#!/usr/bin/env bash
# Capture the live LVGL screen from the board into docs/images/
# Requires a firmware build with -DTOUCHDECK_ENABLE_SCREENSHOT=1 (off in prod).
#   cd firmware && pio run -e usb -t upload --project-option "build_flags=-DTOUCHDECK_ENABLE_SCREENSHOT=1"
# Or temporarily add that flag under [env] in platformio.ini.
#
# Usage:
#   ./scripts/capture-screenshot.sh [host] [basename] [page]
# Examples:
#   ./scripts/capture-screenshot.sh
#   ./scripts/capture-screenshot.sh 192.168.0.183 media-page 0
#   ./scripts/capture-screenshot.sh 192.168.0.183 home-grid 1
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${1:-touchdeck.local}"
BASE="${2:-home-grid}"
PAGE="${3:-}"
OUT_DIR="$ROOT/docs/images"
BMP="$OUT_DIR/${BASE}.bmp"
PNG="$OUT_DIR/${BASE}.png"
PPM="$OUT_DIR/.${BASE}-tmp.ppm"

mkdir -p "$OUT_DIR"
URL="http://$HOST/api/screenshot"
if [[ -n "$PAGE" ]]; then
  URL="${URL}?page=${PAGE}"
fi
echo "GET $URL …"
curl -fsS --max-time 90 -o "$BMP" "$URL"
echo "Downloaded $(wc -c < "$BMP") bytes → $BMP"

python3 - "$BMP" "$PPM" <<'PY'
import struct, sys
from pathlib import Path
bmp_path, ppm_path = Path(sys.argv[1]), Path(sys.argv[2])
data = bmp_path.read_bytes()
off = struct.unpack_from("<I", data, 10)[0]
w, h = struct.unpack_from("<ii", data, 18)
h = abs(h)
row = w * 2
px = memoryview(data)[off : off + row * h]
out = bytearray(w * h * 3)
for y in range(h):
    base = y * row
    for x in range(w):
        i = base + x * 2
        v = px[i] | (px[i + 1] << 8)
        o = (y * w + x) * 3
        out[o] = ((v >> 11) & 0x1F) * 255 // 31
        out[o + 1] = ((v >> 5) & 0x3F) * 255 // 63
        out[o + 2] = (v & 0x1F) * 255 // 31
ppm_path.write_bytes(f"P6\n{w} {h}\n255\n".encode() + out)
print(f"{w}x{h}")
PY

sips -s format png "$PPM" --out "$PNG" >/dev/null
rm -f "$PPM"
echo "Wrote $PNG"
