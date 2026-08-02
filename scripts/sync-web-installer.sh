#!/usr/bin/env bash
# Sync web/install → worker public and optionally set remote firmware URL in manifest template.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/web/install"
DEST="$ROOT/web/worker/public"
VERSION="${VERSION:-$(grep -E '^#define FIRMWARE_VERSION' "$ROOT/firmware/include/version.h" | sed 's/.*"\(.*\)".*/\1/')}"

rm -rf "$DEST"
mkdir -p "$DEST"
cp -R "$SRC/." "$DEST/"

# Strip firmwareBaseUrl from static copy — Worker injects at runtime from env.
python3 - <<PY
import json
from pathlib import Path
p = Path("$DEST/manifest.json")
data = json.loads(p.read_text())
data["version"] = "$VERSION"
data.pop("firmwareBaseUrl", None)
p.write_text(json.dumps(data, indent=2) + "\n")
print(f"Synced installer v$VERSION → $DEST")
PY
