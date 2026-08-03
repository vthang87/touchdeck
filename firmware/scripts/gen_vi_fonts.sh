#!/usr/bin/env bash
# Regenerate Vietnamese UI fonts (DejaVu Sans subset) for LVGL 8.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONT="${ROOT}/.pio/libdeps/usb/lvgl/scripts/built_in_font/DejaVuSans.ttf"
OUT="${ROOT}/src/ui/fonts"
# ASCII + Latin-1 + Latin Extended-A/B + Vietnamese + common punctuation
# (en/em dash, quotes — YouTube/Safari titles often use these)
RANGE='0x20-0x7E,0xA0-0xFF,0x100-0x24F,0x1EA0-0x1EF9,0x2010-0x2015,0x2018-0x2019,0x201C-0x201D,0x2026'

if [[ ! -f "$FONT" ]]; then
  echo "Missing $FONT — run: pio pkg install -e usb" >&2
  exit 1
fi

for SZ in 14 20 28; do
  echo "ui_font_vi_${SZ}…"
  npx --yes lv_font_conv@1.5.2 \
    --font "$FONT" \
    --size "$SZ" \
    --bpp 2 \
    --format lvgl \
    -r "$RANGE" \
    --no-compress \
    --lv-include lvgl.h \
    -o "${OUT}/ui_font_vi_${SZ}.c"
done
echo "Done."
