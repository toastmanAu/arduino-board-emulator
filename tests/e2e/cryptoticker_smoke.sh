#!/usr/bin/env bash
# M2.C E2E: cryptoticker_smoke must build and run to completion.
set -euo pipefail

cd "$(dirname "$0")/../.."

# Ensure ArduinoJson is installed (CI installs this in the workflow).
if ! arduino-cli lib list 2>&1 | grep -q ArduinoJson; then
    echo "❌ ArduinoJson not installed. Run: arduino-cli lib install ArduinoJson"
    exit 1
fi

# Build via boardghost.
cargo run -p boardghost-cli --quiet -- build \
    --board st7789_esp32s3_sim \
    examples/cryptoticker_smoke

# Run and capture stdout. The sketch self-exits after 200 loop iterations.
TMPDIR=$(mktemp -d)
cargo run -p boardghost-cli --quiet -- run \
    --board st7789_esp32s3_sim \
    --screenshot "$TMPDIR/shot.png" \
    examples/cryptoticker_smoke \
    > "$TMPDIR/out.log" 2>&1 || true

if grep -q "cryptoticker_smoke: done" "$TMPDIR/out.log"; then
    echo "✅ cryptoticker_smoke E2E passed"
    rm -rf "$TMPDIR"
    exit 0
fi
echo "❌ cryptoticker_smoke did not reach end:"
tail -40 "$TMPDIR/out.log"
exit 1
